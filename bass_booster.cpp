#include <windows.h>
#include <mmeapi.h>
#include <dsound.h>
#include <cmath>
#include <fstream>
#include <string>

// Struct for DSP Settings
struct Config {
    bool enabled = true;
    float bassGainDb = 6.0f;
    float cutoffFreq = 100.0f;
    float masterVolume = 0.5f; // Set lower by default for immediate hearing test
} g_config;

// BiQuad Low-Shelf Filter
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

    g_filter.SetupLowShelf(44100.0f, g_config.bassGainDb > 0.0f ? g_config.bassGainDb : 1.0f, g_config.bassGainDb);
}

// Function to process 16-bit PCM buffer directly in memory
void ApplyDSP(void* pAudio, DWORD bytes) {
    if (!g_config.enabled || !pAudio || bytes == 0) return;

    short* pSamples = static_cast<short*>(pAudio);
    DWORD sampleCount = bytes / sizeof(short);

    for (DWORD i = 0; i < sampleCount; ++i) {
        float sampleFloat = static_cast<float>(pSamples[i]);
        
        // 1. Bass Boost
        float processed = g_filter.Process(sampleFloat, i % 2);
        
        // 2. Master Volume Adjuster
        processed *= g_config.masterVolume;

        // Hard Limit Guard
        if (processed > 32767.0f) processed = 32767.0f;
        if (processed < -32768.0f) processed = -32768.0f;

        pSamples[i] = static_cast<short>(processed);
    }
}

// VTable Interception for IDirectSoundBuffer::Unlock
typedef HRESULT (STDMETHODCALLTYPE *pfnUnlock)(IDirectSoundBuffer* pThis, LPVOID pvAudioPtr1, DWORD dwAudioBytes1, LPVOID pvAudioPtr2, DWORD dwAudioBytes2);
pfnUnlock g_OriginalUnlock = nullptr;

HRESULT STDMETHODCALLTYPE HookedUnlock(IDirectSoundBuffer* pThis, LPVOID pvAudioPtr1, DWORD dwAudioBytes1, LPVOID pvAudioPtr2, DWORD dwAudioBytes2) {
    if (pvAudioPtr1 && dwAudioBytes1 > 0) {
        ApplyDSP(pvAudioPtr1, dwAudioBytes1);
    }
    if (pvAudioPtr2 && dwAudioBytes2 > 0) {
        ApplyDSP(pvAudioPtr2, dwAudioBytes2);
    }
    return g_OriginalUnlock(pThis, pvAudioPtr1, dwAudioBytes1, pvAudioPtr2, dwAudioBytes2);
}

// Hook IDirectSoundBuffer VTable
void HookBufferVTable(IDirectSoundBuffer* pBuffer) {
    if (!pBuffer) return;
    void** vtable = *reinterpret_cast<void***>(pBuffer);
    
    // Unlock is slot 19 in IDirectSoundBuffer VTable
    if (vtable && vtable[19] != HookedUnlock) {
        DWORD oldProtect;
        VirtualProtect(&vtable[19], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
        g_OriginalUnlock = reinterpret_cast<pfnUnlock>(vtable[19]);
        vtable[19] = reinterpret_cast<void*>(HookedUnlock);
        VirtualProtect(&vtable[19], sizeof(void*), PAGE_EXECUTE_READ, &oldProtect);
    }
}

// DllMain Initialization
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadOrGenerateConfig();
    }
    return TRUE;
}
