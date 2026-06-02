#include "include/flutter_volume_controller/volume_controller.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <string>
#include <algorithm>
#include <cstdio>

#define WM_VOLUME_CHANGED (WM_APP + 999)
#define WM_DEFAULT_DEVICE_CHANGED (WM_APP + 998)

namespace flutter_volume_controller {
	static std::wstring g_current_device_id = L"";

	class AudioNotificationClient;
	static AudioNotificationClient* g_notification_client = NULL;
	static IMMDeviceEnumerator* g_device_enumerator = NULL;

	class AudioNotificationClient : public IMMNotificationClient {
	private:
		LONG _cRef;
	public:
		AudioNotificationClient() : _cRef(1) {}
		virtual ~AudioNotificationClient() {}

		// IUnknown methods
		STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
			if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
				*ppv = static_cast<IMMNotificationClient*>(this);
				AddRef();
				return S_OK;
			}
			*ppv = NULL;
			return E_NOINTERFACE;
		}
		STDMETHODIMP_(ULONG) AddRef() {
			return InterlockedIncrement(&_cRef);
		}
		STDMETHODIMP_(ULONG) Release() {
			ULONG ulRef = InterlockedDecrement(&_cRef);
			if (ulRef == 0) {
				delete this;
			}
			return ulRef;
		}

		// IMMNotificationClient methods
		STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId) {
			if (flow == eRender && (role == eConsole || role == eMultimedia)) {
				HWND hwnd = VolumeController::GetInstance().GetHwnd();
				if (hwnd != NULL) {
					PostMessage(hwnd, WM_DEFAULT_DEVICE_CHANGED, 0, 0);
				}
			}
			return S_OK;
		}

		STDMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwState) { return S_OK; }
		STDMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId) { return S_OK; }
		STDMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId) { return S_OK; }
		STDMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) { return S_OK; }
	};

	VolumeController::VolumeController() : endpoint_volume(NULL), volume_notification(NULL), hwnd(NULL), callback(nullptr) {}

	VolumeController& VolumeController::GetInstance() {
		static VolumeController instance;
		return instance;
	}

	bool VolumeController::RegisterController() {
		HRESULT hr = E_FAIL;
		IMMDevice* default_device = NULL;

		if (g_device_enumerator == NULL) {
			CoInitialize(NULL);
			hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
				(LPVOID*)&g_device_enumerator);
			if (FAILED(hr)) {
				return false;
			}
			g_notification_client = new AudioNotificationClient();
			g_device_enumerator->RegisterEndpointNotificationCallback(g_notification_client);
		}

		hr = g_device_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &default_device);
		if (FAILED(hr)) {
			return false;
		}

		LPWSTR device_id = NULL;
		hr = default_device->GetId(&device_id);
		std::wstring new_device_id = L"";
		if (SUCCEEDED(hr) && device_id != NULL) {
			new_device_id = device_id;
			CoTaskMemFree(device_id);
		}

		if (new_device_id != g_current_device_id || endpoint_volume == NULL) {
			if (endpoint_volume != NULL) {
				if (volume_notification != NULL) {
					endpoint_volume->UnregisterControlChangeNotify(volume_notification);
				}
				endpoint_volume->Release();
				endpoint_volume = NULL;
			}

			hr = default_device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL,
				(LPVOID*)&endpoint_volume);

			if (SUCCEEDED(hr) && endpoint_volume != NULL) {
				g_current_device_id = new_device_id;
				if (volume_notification != NULL) {
					endpoint_volume->RegisterControlChangeNotify(volume_notification);
				}
			}
		}

		default_device->Release();

		return endpoint_volume != NULL;
	}

	bool VolumeController::RegisterNotification(VolumeCallback cb) {
		HRESULT hr = E_FAIL;

		if (!cb) {
			return false;
		}

		this->callback = cb;

		RegisterController();

		if (!endpoint_volume) {
			return false;
		}

		volume_notification = new VolumeNotification(cb);
		hr = endpoint_volume->RegisterControlChangeNotify(volume_notification);
		if (FAILED(hr)) {
			return false;
		}

		return true;
	}

	void VolumeController::DisposeController() {
		if (endpoint_volume) {
			DisposeNotification();
			endpoint_volume->Release();
			endpoint_volume = NULL;
		}

		if (g_device_enumerator != NULL) {
			if (g_notification_client != NULL) {
				g_device_enumerator->UnregisterEndpointNotificationCallback(g_notification_client);
				g_notification_client->Release();
				g_notification_client = NULL;
			}
			g_device_enumerator->Release();
			g_device_enumerator = NULL;
		}

		CoUninitialize();
	}

	void VolumeController::DisposeNotification() {
		if (volume_notification) {
			if (endpoint_volume) {
				endpoint_volume->UnregisterControlChangeNotify(volume_notification);
			}
			volume_notification->Release();
			volume_notification = NULL;
		}
		this->callback = nullptr;
	}

	void VolumeController::PostVolumeMessage(float volume) {
		if (hwnd != NULL) {
			WPARAM wparam = 0;
			std::memcpy(&wparam, &volume, sizeof(float));
			// fprintf(stderr, "[VolumeController] PostMessage WM_VOLUME_CHANGED volume=%.4f hwnd=%p\n", volume, hwnd);
			// fflush(stderr);
			PostMessage(hwnd, WM_VOLUME_CHANGED, wparam, 0);
		} else {
			fprintf(stderr, "[VolumeController] PostVolumeMessage: hwnd is NULL, calling callback directly\n");
			fflush(stderr);
			if (callback) {
				callback(volume);
			}
		}
	}

	void VolumeController::NotifyVolumeChanged(float volume) {
		// fprintf(stderr, "[VolumeController] NotifyVolumeChanged volume=%.4f callback=%s\n", volume, callback ? "set" : "null");
		// fflush(stderr);
		if (callback) {
			callback(volume);
		}
	}

	bool VolumeController::SetVolume(float volume) {
		HRESULT hr = E_FAIL;

		RegisterController();

		if (!endpoint_volume) {
			return false;
		}

		if (!SetMute(false)) {
			return false;
		}

		float normalized_volume = std::min<float>(std::max<float>(0, volume), 1);
		hr = endpoint_volume->SetMasterVolumeLevelScalar(normalized_volume, NULL);

		if (FAILED(hr)) {
			return false;
		}

		return true;
	}

	bool VolumeController::SetMaxVolume() {
		HRESULT hr = E_FAIL;
		UINT current_step, step_count;

		RegisterController();

		if (!endpoint_volume) {
			return false;
		}

		hr = endpoint_volume->GetVolumeStepInfo(&current_step, &step_count);
		if (FAILED(hr)) {
			return false;
		}

		if (!SetMute(false)) {
			return false;
		}

		for (UINT index = current_step; index < step_count; index++) {
			if (!SetVolumeUpBySystemStep()) {
				return false;
			}
		}

		return true;
	}

	bool VolumeController::SetMinVolume() {
		HRESULT hr = E_FAIL;
		UINT current_step, step_count;

		RegisterController();

		if (!endpoint_volume) {
			return false;
		}

		hr = endpoint_volume->GetVolumeStepInfo(&current_step, &step_count);
		if (FAILED(hr)) {
			return false;
		}

		if (!SetMute(false)) {
			return false;
		}

		for (UINT index = current_step; index > 0; index--) {
			if (!SetVolumeDownBySystemStep()) {
				return false;
			}
		}

		return true;
	}

	bool VolumeController::SetVolumeUp(float step) {
		HRESULT hr = E_FAIL;

		RegisterController();

		if (!endpoint_volume) {
			return false;
		}

		auto current_volume = GetCurrentVolume();
		if (!current_volume.has_value()) {
			return false;
		}

		float volume = current_volume.value();
		float normalized_volume = (1 - volume) < step ? 1 : volume + step;

		hr = endpoint_volume->SetMasterVolumeLevelScalar(normalized_volume, NULL);
		if (FAILED(hr)) {
			return false;
		}

		return true;
	}

	bool VolumeController::SetVolumeDown(float step) {
		HRESULT hr = E_FAIL;

		RegisterController();

		if (!endpoint_volume) {
			return false;
		}

		auto current_volume = GetCurrentVolume();
		if (!current_volume.has_value()) {
			return false;
		}

		float volume = current_volume.value();
		float normalized_volume = volume < step ? 0 : volume - step;

		hr = endpoint_volume->SetMasterVolumeLevelScalar(normalized_volume, NULL);
		if (FAILED(hr)) {
			return false;
		}

		return true;
	}

	bool VolumeController::SetVolumeUpBySystemStep() {
		HRESULT hr = E_FAIL;

		RegisterController();

		if (!endpoint_volume) {
			return false;
		}

		hr = endpoint_volume->VolumeStepUp(NULL);
		if (FAILED(hr)) {
			return false;
		}

		return true;
	}

	bool VolumeController::SetVolumeDownBySystemStep() {
		HRESULT hr = E_FAIL;

		RegisterController();

		if (!endpoint_volume) {
			return false;
		}

		hr = endpoint_volume->VolumeStepDown(NULL);
		if (FAILED(hr)) {
			return false;
		}

		return true;
	}

	bool VolumeController::SetMute(bool is_mute) {
		HRESULT hr = E_FAIL;

		RegisterController();

		if (!endpoint_volume) {
			return false;
		}

		if (is_mute) {
			hr = endpoint_volume->SetMute(TRUE, NULL);
		}
		else {
			hr = endpoint_volume->SetMute(FALSE, NULL);
		}

		if (FAILED(hr)) {
			return false;
		}

		return true;
	}

	bool VolumeController::ToggleMute() {
		std::optional<bool> is_muted = GetMute();

		if (!is_muted.has_value()) {
			return false;
		}

		return SetMute(!is_muted.value());
	}

	std::optional<float> VolumeController::GetCurrentVolume() {
		HRESULT hr = E_FAIL;
		float current_volume = 0.0f;

		RegisterController();

		if (!endpoint_volume) {
			return std::nullopt;
		}

		hr = endpoint_volume->GetMasterVolumeLevelScalar(&current_volume);
		if (FAILED(hr)) {
			return std::nullopt;
		}

		return current_volume;
	}

	std::optional<bool> VolumeController::GetMute() {
		HRESULT hr = E_FAIL;
		BOOL is_muted;

		RegisterController();

		if (!endpoint_volume) {
			return std::nullopt;
		}

		hr = endpoint_volume->GetMute(&is_muted);
		if (FAILED(hr)) {
			return std::nullopt;
		}

		return is_muted;
	}
}
