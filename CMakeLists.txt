#include <windows.h>
#include <mmeapi.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <cmath>
#include <fstream>
#include <string>
#include "MinHook.h"

#ifndef AUDCLNT_BUFFERFLAGS_SILENT
#define AUDCLNT_BUFFERFLAGS_SILENT 0x2
#endif

struct Config {
    bool enabled = true;
    float bassGainDb = 10.0f;
    float cutoffFreq = 100.0f;
    float masterVolume = 0.2f; // Low volume for instant audio test
} g_config;

struct BiquadFilter {
    float b0, b1, b2, a1, a2;
    float x1[2] = {0}, x2[2] = {0};
    float y1[2] = {0}, y2[2] = {0};

    void SetupLowShelf(float sampleRate, float cutoffFreq, float gainDb) {
        if (gainDb == 0.0f) {
            b0 = 1.0f; b1 = a1 = a2 = b2 = 0.0f;
            return;
        }
        float A = powf(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * 3.14159265359f * cutoffFreq / sampleRate;
        float alpha = sinf(w0) / 2.0f * sqrtf(2.0f);
        float cosw0 = cosf(w0);
        float beta = sqrtf(A) / 0.707f;

        float a0 = (A + 1.0f) + (A - 1.0f) * cosw0 + beta * alpha;
        b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 + beta * alpha)) / a0;
        b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0)) / a0;
        b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw0 - beta * alpha)) / a0;
        a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0)) / a0;
        a2 = ((A + 1.0f) + (A - 1.0f) * cosw0 - beta * alpha) / a0;
    }

    inline float Process(float in, int ch) {
        float out = b0 * in + b1 * x1[ch] + b2 * x2[ch] - a1 * y1[ch] - a2 * y2[ch];
        x2[ch] = x1[ch]; x1[ch] = in;
        y2[ch] = y1[ch]; y1[ch] = out;
        return out;
    }
} g_filter;

void LoadOrGenerateConfig() {
    char modulePath[MAX_PATH];
    GetModuleFileNameA(NULL, modulePath, MAX_PATH);
    std::string path(modulePath);
    std::string iniPath = path.substr(0, path.find_last_of("\\/")) + "\\bass_boost.ini";

    DWORD attrib = GetFileAttributesA(iniPath.c_str());
    if (attrib == INVALID_FILE_ATTRIBUTES) {
        std::ofstream iniFile(iniPath);
        if (iniFile.is_open()) {
            iniFile << "[BassBooster]\nEnabled=1\nBassGainDb=10.0\nCutoffFreq=100.0\nMasterVolume=0.2\n";
            iniFile.close();
        }
    }

    g_config.enabled = GetPrivateProfileIntA("BassBooster", "Enabled", 1, iniPath.c_str()) != 0;
    
    char buffer[32];
    GetPrivateProfileStringA("BassBooster", "BassGainDb", "10.0", buffer, sizeof(buffer), iniPath.c_str());
    g_config.bassGainDb = static_cast<float>(atof(buffer));

    GetPrivateProfileStringA("BassBooster", "CutoffFreq", "100.0", buffer, sizeof(buffer), iniPath.c_str());
    g_config.cutoffFreq = static_cast<float>(atof(buffer));

    GetPrivateProfileStringA("BassBooster", "MasterVolume", "0.2", buffer, sizeof(buffer), iniPath.c_str());
    g_config.masterVolume = static_cast<float>(atof(buffer));

    g_filter.SetupLowShelf(44100.0f, g_config.cutoffFreq, g_config.bassGainDb);
}

void ProcessPCMFloat(float* pSamples, DWORD sampleCount) {
    if (!g_config.enabled || !pSamples || sampleCount == 0) return;

    for (DWORD i = 0; i < sampleCount; ++i) {
        float sampleFloat = pSamples[i];
        float processed = g_filter.Process(sampleFloat, i % 2);
        processed *= g_config.masterVolume;

        if (processed > 1.0f) processed = 1.0f;
        if (processed < -1.0f) processed = -1.0f;

        pSamples[i] = processed;
    }
}

// WASAPI Hooks
typedef HRESULT (STDMETHODCALLTYPE *pfnReleaseBuffer)(IAudioRenderClient* pThis, UINT32 NumFramesWritten, DWORD dwFlags);
pfnReleaseBuffer g_OriginalReleaseBuffer = nullptr;

thread_local BYTE* t_pAudioBuffer = nullptr;

typedef HRESULT (STDMETHODCALLTYPE *pfnGetBuffer)(IAudioRenderClient* pThis, UINT32 NumFramesRequested, BYTE** ppData);
pfnGetBuffer g_OriginalGetBuffer = nullptr;

HRESULT STDMETHODCALLTYPE HookedGetBuffer(IAudioRenderClient* pThis, UINT32 NumFramesRequested, BYTE** ppData) {
    HRESULT hr = g_OriginalGetBuffer(pThis, NumFramesRequested, ppData);
    if (SUCCEEDED(hr) && ppData) {
        t_pAudioBuffer = *ppData;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedReleaseBuffer(IAudioRenderClient* pThis, UINT32 NumFramesWritten, DWORD dwFlags) {
    if (t_pAudioBuffer && NumFramesWritten > 0 && !(dwFlags & AUDCLNT_BUFFERFLAGS_SILENT)) {
        DWORD sampleCount = NumFramesWritten * 2;
        ProcessPCMFloat(reinterpret_cast<float*>(t_pAudioBuffer), sampleCount);
    }
    t_pAudioBuffer = nullptr;
    return g_OriginalReleaseBuffer(pThis, NumFramesWritten, dwFlags);
}

// Hook IAudioClient::GetService to grab IAudioRenderClient VTable dynamically
typedef HRESULT (STDMETHODCALLTYPE *pfnGetService)(IAudioClient* pThis, REFIID riid, void** ppv);
pfnGetService g_OriginalGetService = nullptr;

HRESULT STDMETHODCALLTYPE HookedGetService(IAudioClient* pThis, REFIID riid, void** ppv) {
    HRESULT hr = g_OriginalGetService(pThis, riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv) {
        if (riid == __uuidof(IAudioRenderClient)) {
            IAudioRenderClient* pRenderClient = static_cast<IAudioRenderClient*>(*ppv);
            void** vtable = *reinterpret_cast<void***>(pRenderClient);
            
            if (vtable && !g_OriginalReleaseBuffer) {
                // GetBuffer is VTable slot 3, ReleaseBuffer is VTable slot 4
                MH_CreateHook(vtable[3], (LPVOID)&HookedGetBuffer, (LPVOID*)&g_OriginalGetBuffer);
                MH_CreateHook(vtable[4], (LPVOID)&HookedReleaseBuffer, (LPVOID*)&g_OriginalReleaseBuffer);
                MH_EnableHook(vtable[3]);
                MH_EnableHook(vtable[4]);
            }
        }
    }
    return hr;
}

// CoCreateInstance Hook to capture WASAPI MMDeviceEnumerator creation
typedef HRESULT (WINAPI *pfnCoCreateInstance)(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv);
pfnCoCreateInstance g_OriginalCoCreateInstance = nullptr;

HRESULT WINAPI HookedCoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, REFIID riid, LPVOID *ppv) {
    HRESULT hr = g_OriginalCoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv) {
        if (rclsid == __uuidof(MMDeviceEnumerator)) {
            IMMDeviceEnumerator* pEnumerator = static_cast<IMMDeviceEnumerator*>(*ppv);
            IMMDevice* pDevice = nullptr;
            if (SUCCEEDED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)) && pDevice) {
                IAudioClient* pAudioClient = nullptr;
                if (SUCCEEDED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient)) && pAudioClient) {
                    void** vtable = *reinterpret_cast<void***>(pAudioClient);
                    if (vtable && !g_OriginalGetService) {
                        // GetService is VTable slot 14 in IAudioClient
                        MH_CreateHook(vtable[14], (LPVOID)&HookedGetService, (LPVOID*)&g_OriginalGetService);
                        MH_EnableHook(vtable[14]);
                    }
                    pAudioClient->Release();
                }
                pDevice->Release();
            }
        }
    }
    return hr;
}

void InitUniversalHooks() {
    MH_Initialize();

    HMODULE hOle32 = GetModuleHandleA("ole32.dll");
    if (!hOle32) hOle32 = LoadLibraryA("ole32.dll");

    if (hOle32) {
        void* pCoCreateInstance = (void*)GetProcAddress(hOle32, "CoCreateInstance");
        if (pCoCreateInstance) {
            MH_CreateHook(pCoCreateInstance, (LPVOID)&HookedCoCreateInstance, (LPVOID*)&g_OriginalCoCreateInstance);
            MH_EnableHook(pCoCreateInstance);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadOrGenerateConfig();
        InitUniversalHooks();
    }
    return TRUE;
}
