#include <windows.h>
#include <mmeapi.h>
#include <cmath>
#include <fstream>
#include <string>
#include "MinHook.h"

struct Config {
    bool enabled = true;
    float bassGainDb = 10.0f;
    float cutoffFreq = 100.0f;
    float masterVolume = 0.2f;
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

// Inline audio processor for 16-bit PCM (WinMM)
void ProcessPCM16(short* pSamples, DWORD sampleCount) {
    if (!g_config.enabled || !pSamples || sampleCount == 0) return;

    for (DWORD i = 0; i < sampleCount; ++i) {
        float sampleFloat = static_cast<float>(pSamples[i]);
        float processed = g_filter.Process(sampleFloat, i % 2);
        processed *= g_config.masterVolume;

        if (processed > 32767.0f) processed = 32767.0f;
        if (processed < -32768.0f) processed = -32768.0f;

        pSamples[i] = static_cast<short>(processed);
    }
}

// Hook winmm.dll (waveOutWrite) safely
typedef MMRESULT (WINAPI *pfnWaveOutWrite)(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh);
pfnWaveOutWrite g_OriginalWaveOutWrite = nullptr;

MMRESULT WINAPI HookedWaveOutWrite(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    if (pwh && pwh->lpData && pwh->dwBufferLength > 0) {
        ProcessPCM16(reinterpret_cast<short*>(pwh->lpData), pwh->dwBufferLength / sizeof(short));
    }
    return g_OriginalWaveOutWrite(hwo, pwh, cbwh);
}

void InitHooks() {
    if (MH_Initialize() != MH_OK) return;

    // Safely hook WinMM without touching COM or system-wide routines
    HMODULE hWinMM = GetModuleHandleA("winmm.dll");
    if (!hWinMM) hWinMM = LoadLibraryA("winmm.dll");

    if (hWinMM) {
        void* pWaveOutWrite = (void*)GetProcAddress(hWinMM, "waveOutWrite");
        if (pWaveOutWrite) {
            MH_CreateHook(pWaveOutWrite, (LPVOID)&HookedWaveOutWrite, (LPVOID*)&g_OriginalWaveOutWrite);
            MH_EnableHook(pWaveOutWrite);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadOrGenerateConfig();
        InitHooks();
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}
