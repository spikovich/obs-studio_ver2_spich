#include "../../obs-internal.h"
#include "wasapi-output.h"

#include <propsys.h>

#ifdef __MINGW32__

#ifndef DEFINE_PROPERTYKEY
#define DEFINE_PROPERTYKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid) \
    const PROPERTYKEY name = { { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }, pid }
#endif

DEFINE_PROPERTYKEY(PKEY_Device_FriendlyName,
		   0xa45c254e, 0xdf1c, 0x4efd,
		   0x80, 0x20, 0x67, 0xd1,
		   0x46, 0xa8, 0x50, 0xe0, 14);

#else

#include <functiondiscoverykeys_devpkey.h>

#endif

#define SAFE_RELEASE(p) if ((p)) { (p)->lpVtbl->Release(p); (p) = NULL; }
#define SAFE_FREE(p) if ((p)) { bfree(p); (p) = NULL; }

static bool get_device_info(obs_enum_audio_device_cb cb, void *data,
			    IMMDeviceCollection *collection, UINT idx)
{
	HRESULT hr;
	IMMDevice *device = NULL;
	IPropertyStore *store = NULL;
	PROPVARIANT name_var;
	WCHAR *w_id = NULL;
	char *utf8_name = NULL;
	char *utf8_id = NULL;
	bool continue_enum = true;

	hr = collection->lpVtbl->Item(collection, idx, &device);
	if (FAILED(hr)) {
		blog(LOG_ERROR, "Failed to get device at index %u: 0x%08X", idx, hr);
		goto cleanup;
	}

	hr = device->lpVtbl->GetId(device, &w_id);
	if (FAILED(hr)) {
		blog(LOG_ERROR, "Failed to get device ID: 0x%08X", hr);
		goto cleanup;
	}

	hr = device->lpVtbl->OpenPropertyStore(device, STGM_READ, &store);
	if (FAILED(hr)) {
		blog(LOG_ERROR, "Failed to open property store: 0x%08X", hr);
		goto cleanup;
	}

	PropVariantInit(&name_var);
	hr = store->lpVtbl->GetValue(store, &PKEY_Device_FriendlyName, &name_var);
	if (FAILED(hr)) {
		blog(LOG_ERROR, "Failed to get device friendly name: 0x%08X", hr);
		goto cleanup;
	}

	if (name_var.vt != VT_LPWSTR) {
		blog(LOG_ERROR, "Device friendly name has unexpected variant type: %u", name_var.vt);
		goto cleanup;
	}

	if (os_wcs_to_utf8_ptr(w_id, 0, &utf8_id) == 0) {
		blog(LOG_ERROR, "Failed to convert device ID to UTF-8");
		goto cleanup;
	}

	if (os_wcs_to_utf8_ptr(name_var.pwszVal, 0, &utf8_name) == 0) {
		blog(LOG_ERROR, "Failed to convert device friendly name to UTF-8");
		goto cleanup;
	}

	continue_enum = cb(data, utf8_name, utf8_id);

cleanup:
	SAFE_FREE(utf8_name);
	SAFE_FREE(utf8_id);
	PropVariantClear(&name_var);
	SAFE_RELEASE(store);
	SAFE_RELEASE(device);
	if (w_id)
		CoTaskMemFree(w_id);

	return continue_enum;
}

void obs_enum_audio_monitoring_devices(obs_enum_audio_device_cb cb, void *data)
{
	HRESULT hr, hr_coinit;
	IMMDeviceEnumerator *enumerator = NULL;
	IMMDeviceCollection *collection = NULL;
	UINT count = 0;

	hr_coinit = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr_coinit) && hr_coinit != RPC_E_CHANGED_MODE) {
		blog(LOG_ERROR, "Failed to initialize COM library: 0x%08X", hr_coinit);
		return;
	}

	hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
			      &IID_IMMDeviceEnumerator, (void **)&enumerator);
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
	SAFE_RELEASE(collection);
	SAFE_RELEASE(enumerator);
	if (SUCCEEDED(hr_coinit))
		CoUninitialize();
}

static void get_default_id(char **p_id)
{
	HRESULT hr, hr_coinit;
	IMMDeviceEnumerator *enumerator = NULL;
	IMMDevice *device = NULL;
	WCHAR *w_id = NULL;
	char *utf8_id = NULL;

	if (!p_id || *p_id)
		return;

	hr_coinit = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr_coinit) && hr_coinit != RPC_E_CHANGED_MODE) {
		blog(LOG_ERROR, "Failed to initialize COM library: 0x%08X", hr_coinit);
		return;
	}

	hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
			      &IID_IMMDeviceEnumerator, (void **)&enumerator);
	if (FAILED(hr)) {
		blog(LOG_ERROR, "Failed to create IMMDeviceEnumerator: 0x%08X", hr);
		goto cleanup;
	}

	hr = enumerator->lpVtbl->GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);
	if (FAILED(hr)) {
		blog(LOG_ERROR, "Failed to get default audio endpoint: 0x%08X", hr);
		goto cleanup;
	}

	hr = device->lpVtbl->GetId(device, &w_id);
	if (FAILED(hr)) {
		blog(LOG_ERROR, "Failed to get default device ID: 0x%08X", hr);
		goto cleanup;
	}

	if (os_wcs_to_utf8_ptr(w_id, 0, &utf8_id) == 0) {
		blog(LOG_ERROR, "Failed to convert default device ID to UTF-8");
		goto cleanup;
	}

	*p_id = utf8_id;
	utf8_id = NULL;  // Ownership transferred to caller

cleanup:
	SAFE_FREE(utf8_id);
	if (w_id)
		CoTaskMemFree(w_id);
	SAFE_RELEASE(device);
	SAFE_RELEASE(enumerator);
	if (SUCCEEDED(hr_coinit))
		CoUninitialize();

	if (!*p_id)
		*p_id = bzalloc(1);  // Allocate empty string if failed
}

bool devices_match(const char *id1, const char *id2)
{
	char *default_id1 = NULL;
	char *default_id2 = NULL;
	const char *comp_id1 = id1;
	const char *comp_id2 = id2;
	bool match = false;

	if (!id1 || !id2)
		return false;

	if (strcmp(id1, "default") == 0) {
		get_default_id(&default_id1);
		comp_id1 = default_id1;
	}

	if (strcmp(id2, "default") == 0) {
		get_default_id(&default_id2);
		comp_id2 = default_id2;
	}

	match = strcmp(comp_id1, comp_id2) == 0;

	SAFE_FREE(default_id1);
	SAFE_FREE(default_id2);

	return match;
}
