#include "../../obs-internal.h"
#include "wasapi-output.h"

#include <propsys.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <comdef.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

static bool GetDeviceInfo(obs_enum_audio_device_cb cb, void* data, IMMDeviceCollection* collection, UINT idx)
{
    ComPtr<IMMDevice> device;
    HRESULT hr = collection->Item(idx, &device);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get device at index %u: 0x%08X", idx, hr);
        return true; // Continue enumeration
    }

    LPWSTR w_id = nullptr;
    hr = device->GetId(&w_id);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get device ID: 0x%08X", hr);
        return true;
    }
    std::wstring wstr_id(w_id);
    CoTaskMemFree(w_id);

    ComPtr<IPropertyStore> store;
    hr = device->OpenPropertyStore(STGM_READ, &store);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to open property store: 0x%08X", hr);
        return true;
    }

    PROPVARIANT name_var;
    PropVariantInit(&name_var);
    hr = store->GetValue(PKEY_Device_FriendlyName, &name_var);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get device friendly name: 0x%08X", hr);
        PropVariantClear(&name_var);
        return true;
    }

    std::wstring wstr_name(name_var.pwszVal);
    PropVariantClear(&name_var);

    // Convert wide strings to UTF-8
    std::string utf8_id = os_wcs_to_utf8(wstr_id.c_str());
    std::string utf8_name = os_wcs_to_utf8(wstr_name.c_str());

    bool cont = cb(data, utf8_name.c_str(), utf8_id.c_str());
    return cont;
}

void obs_enum_audio_monitoring_devices(obs_enum_audio_device_cb cb, void* data)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to create IMMDeviceEnumerator: 0x%08X", hr);
        return;
    }

    ComPtr<IMMDeviceCollection> collection;
    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to enumerate audio endpoints: 0x%08X", hr);
        return;
    }

    UINT count = 0;
    hr = collection->GetCount(&count);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get device count: 0x%08X", hr);
        return;
    }

    for (UINT i = 0; i < count; ++i) {
        if (!GetDeviceInfo(cb, data, collection.Get(), i)) {
            break;
        }
    }
}

static std::string GetDefaultDeviceId()
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to create IMMDeviceEnumerator: 0x%08X", hr);
        return "";
    }

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get default audio endpoint: 0x%08X", hr);
        return "";
    }

    LPWSTR w_id = nullptr;
    hr = device->GetId(&w_id);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "Failed to get default device ID: 0x%08X", hr);
        return "";
    }
    std::wstring wstr_id(w_id);
    CoTaskMemFree(w_id);

    return os_wcs_to_utf8(wstr_id.c_str());
}

bool devices_match(const char* id1, const char* id2)
{
    if (!id1 || !id2) {
        return false;
    }

    std::string id1_str = (strcmp(id1, "default") == 0) ? GetDefaultDeviceId() : id1;
    std::string id2_str = (strcmp(id2, "default") == 0) ? GetDefaultDeviceId() : id2;

    return id1_str == id2_str;
}
