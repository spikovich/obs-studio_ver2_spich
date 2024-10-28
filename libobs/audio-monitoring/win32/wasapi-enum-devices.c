#include "../../obs-internal.h"
#include "wasapi-output.h"

#include <propsys.h>

#ifdef __MINGW32__

#ifdef DEFINE_PROPERTYKEY
#undef DEFINE_PROPERTYKEY
#endif
#define DEFINE_PROPERTYKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) \
    const PROPERTYKEY name = { { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }, pid }

DEFINE_PROPERTYKEY(PKEY_Device_FriendlyName,
                   0xa45c254e, 0xdf1c, 0x4efd,
                   0x80, 0x20, 0x67, 0xd1,
                   0x46, 0xa8, 0x50, 0xe0, 14);

#else

#include <functiondiscoverykeys_devpkey.h>

#endif

static bool get_device_info(obs_enum_audio_device_cb cb, void *data,
                            IMMDeviceCollection *collection, UINT idx)
{
    IPropertyStore *store = NULL;
    IMMDevice *device = NULL;
    PROPVARIANT name_var;
    WCHAR *w_id = NULL;
    char *utf8_name = NULL;
    char *utf8_id = NULL;
    bool cont = true;
    HRESULT hr;

    hr = collection->lpVtbl->Item(collection, idx, &device);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get device at index %u: 0x%08X", idx, hr);
        cont = true;
        goto cleanup;
    }

    hr = device->lpVtbl->GetId(device, &w_id);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get device ID: 0x%08X", hr);
        cont = true;
        goto cleanup;
    }

    hr = device->lpVtbl->OpenPropertyStore(device, STGM_READ, &store);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to open property store: 0x%08X", hr);
        cont = true;
        goto cleanup;
    }

    PropVariantInit(&name_var);
    hr = store->lpVtbl->GetValue(store, &PKEY_Device_FriendlyName, &name_var);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get device friendly name: 0x%08X", hr);
        cont = true;
        goto cleanup;
    }

    if (name_var.vt != VT_LPWSTR) {
        blog(LOG_ERROR, "Device friendly name has unexpected variant type: %u", name_var.vt);
        cont = true;
        goto cleanup;
    }

    utf8_id = os_wcs_to_utf8(w_id);
    if (!utf8_id) {
        blog(LOG_ERROR, "Failed to convert device ID to UTF-8");
        cont = true;
        goto cleanup;
    }

    utf8_name = os_wcs_to_utf8(name_var.pwszVal);
    if (!utf8_name) {
        blog(LOG_ERROR, "Failed to convert device friendly name to UTF-8");
        cont = true;
        goto cleanup;
    }

    cont = cb(data, utf8_name, utf8_id);

cleanup:
    if (utf8_name)
        bfree(utf8_name);
    if (utf8_id)
        bfree(utf8_id);
    PropVariantClear(&name_var);
    if (store)
        store->lpVtbl->Release(store);
    if (device)
        device->lpVtbl->Release(device);
    if (w_id)
        CoTaskMemFree(w_id);
    return cont;
}

void obs_enum_audio_monitoring_devices(obs_enum_audio_device_cb cb, void *data)
{
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDeviceCollection *collection = NULL;
    UINT count = 0;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void**)&enumerator);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to create IMMDeviceEnumerator: 0x%08X", hr);
        goto cleanup;
    }

    hr = enumerator->lpVtbl->EnumAudioEndpoints(enumerator, eRender,
                                                DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to enumerate audio endpoints: 0x%08X", hr);
        goto cleanup;
    }

    hr = collection->lpVtbl->GetCount(collection, &count);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get device count: 0x%08X", hr);
        goto cleanup;
    }

    for (UINT i = 0; i < count; i++) {
        if (!get_device_info(cb, data, collection, i)) {
            break;
        }
    }

cleanup:
    if (collection)
        collection->lpVtbl->Release(collection);
    if (enumerator)
        enumerator->lpVtbl->Release(enumerator);
}

static void get_default_id(char **p_id)
{
    IMMDeviceEnumerator *immde = NULL;
    IMMDevice *device = NULL;
    WCHAR *w_id = NULL;
    char *utf8_id = NULL;
    HRESULT hr;

    if (*p_id)
        return;

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void**)&immde);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to create IMMDeviceEnumerator: 0x%08X", hr);
        goto cleanup;
    }

    hr = immde->lpVtbl->GetDefaultAudioEndpoint(immde, eRender, eConsole, &device);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get default audio endpoint: 0x%08X", hr);
        goto cleanup;
    }

    hr = device->lpVtbl->GetId(device, &w_id);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get default device ID: 0x%08X", hr);
        goto cleanup;
    }

    utf8_id = os_wcs_to_utf8(w_id);
    if (!utf8_id) {
        blog(LOG_ERROR, "Failed to convert default device ID to UTF-8");
        goto cleanup;
    }

    *p_id = utf8_id;
    utf8_id = NULL;  // Ownership transferred to caller

cleanup:
    if (!*p_id)
        *p_id = bzalloc(1);  // Allocate empty string if failed

    if (utf8_id)
        bfree(utf8_id);
    if (w_id)
        CoTaskMemFree(w_id);
    if (device)
        device->lpVtbl->Release(device);
    if (immde)
        immde->lpVtbl->Release(immde);
}

bool devices_match(const char *id1, const char *id2)
{
    char *default_id1 = NULL;
    char *default_id2 = NULL;
    bool match = false;

    if (!id1 || !id2)
        return false;

    if (strcmp(id1, "default") == 0) {
        get_default_id(&default_id1);
        id1 = default_id1;
    }

    if (strcmp(id2, "default") == 0) {
        get_default_id(&default_id2);
        id2 = default_id2;
    }

    match = strcmp(id1, id2) == 0;

    if (default_id1)
        bfree(default_id1);
    if (default_id2)
        bfree(default_id2);

    return match;
}
