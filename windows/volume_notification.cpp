#include "include/flutter_volume_controller/volume_notification.h"
#include "include/flutter_volume_controller/volume_controller.h"
#include <cstdio>

namespace flutter_volume_controller {
	VolumeNotification::VolumeNotification(VolumeCallback pCallback) : lRef(1), pCallback(pCallback) {}

	STDMETHODIMP_(ULONG) VolumeNotification::AddRef() {
		return InterlockedIncrement(&lRef);
	}

	STDMETHODIMP_(ULONG) VolumeNotification::Release() {
		LONG ref = InterlockedDecrement(&lRef);
		if (ref == 0) {
			delete this;
		}
		return ref;
	}

	STDMETHODIMP VolumeNotification::QueryInterface(REFIID riid, void** ppvObject) {
		if (riid == IID_IUnknown || riid == __uuidof(IAudioEndpointVolumeCallback)) {
			AddRef();
			*ppvObject = static_cast<IUnknown*>(this);
			return S_OK;
		}

		*ppvObject = NULL;
		return E_NOINTERFACE;
	}

	STDMETHODIMP VolumeNotification::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
		if (pNotify == NULL) {
			return E_INVALIDARG;
		}

		// fprintf(stderr, "[VolumeNotification] OnNotify fired: volume=%.4f\n", pNotify->fMasterVolume);
		// fflush(stderr);
		VolumeController::GetInstance().PostVolumeMessage(pNotify->fMasterVolume);
		return S_OK;
	}
}
