#define INITGUID
#define COBJMACROS
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>

int main(void) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) { printf("CoInitializeEx failed 0x%08lx\n", (unsigned long)hr); return 1; }
    IMMDeviceEnumerator *en = NULL;
    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void**)&en);
    if (FAILED(hr)) { printf("enumerator failed 0x%08lx\n", (unsigned long)hr); return 1; }
    IMMDevice *dev = NULL;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev);
    if (FAILED(hr)) { printf("endpoint failed 0x%08lx\n", (unsigned long)hr); return 1; }
    IAudioClient *ac = NULL;
    hr = IMMDevice_Activate(dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&ac);
    if (FAILED(hr)) { printf("activate failed 0x%08lx\n", (unsigned long)hr); return 1; }
    REFERENCE_TIME def = 0, min = 0;
    hr = IAudioClient_GetDevicePeriod(ac, &def, &min);
    if (FAILED(hr)) { printf("period failed 0x%08lx\n", (unsigned long)hr); return 1; }
    WAVEFORMATEX *fmt = NULL;
    hr = IAudioClient_GetMixFormat(ac, &fmt);
    if (FAILED(hr)) { printf("format failed 0x%08lx\n", (unsigned long)hr); return 1; }
    hr = IAudioClient_Initialize(ac, AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, fmt, NULL);
    if (FAILED(hr)) { printf("initialize failed 0x%08lx\n", (unsigned long)hr); return 1; }
    UINT32 frames = 0;
    REFERENCE_TIME stream_latency = 0;
    IAudioClient_GetBufferSize(ac, &frames);
    IAudioClient_GetStreamLatency(ac, &stream_latency);
    printf("wasapi mix=%uHz channels=%u default_period=%.3fms min_period=%.3fms buffer_frames=%u stream_latency=%.3fms\n",
           (unsigned)fmt->nSamplesPerSec, (unsigned)fmt->nChannels,
           (double)def/10000.0, (double)min/10000.0, (unsigned)frames,
           (double)stream_latency/10000.0);
    CoTaskMemFree(fmt);
    IAudioClient_Release(ac);
    IMMDevice_Release(dev);
    IMMDeviceEnumerator_Release(en);
    CoUninitialize();
    return 0;
}
