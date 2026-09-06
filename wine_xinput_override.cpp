#include <windows.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

// Default Config Values
struct Config {
    bool enabled = true;
    float bassGainDb = 6.0f;     // Bass Boost in dB (e.g. 3.0 to 12.0)
    float cutoffFreq = 100.0f;   // Frequency cutoff in Hz
    float masterVolume = 1.0f;   // Volume multiplier (0.1 to 2.0) for debug testing
} g_config;

// BiQuad Filter State for 2 channels (Stereo)
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
        float alpha = sinf(w0) / 2.0f * sqrtf(2.0f); // Q = 0.707 (Standard Butterworth)

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
        x2[ch] = x1[ch];
        x1[ch] = in;
        y2[ch] = y1[ch];
        y1[ch] = out;
        return out;
    }
} g_filter;

// Load or Generate INI Configuration
void LoadOrGenerateConfig() {
    char modulePath[MAX_PATH];
    GetModuleFileNameA(NULL, modulePath, MAX_PATH);
    std::string path(modulePath);
    std::string iniPath = path.substr(0, path.find_last_of("\\/")) + "\\bass_boost.ini";

    // Auto-create INI if missing
    DWORD attrib = GetFileAttributesA(iniPath.c_str());
    if (attrib == INVALID_FILE_ATTRIBUTES) {
        std::ofstream iniFile(iniPath);
        if (iniFile.is_open()) {
            iniFile << "[BassBooster]\n";
            iniFile << "; Enable or disable DSP filter (1 = ON, 0 = OFF)\n";
            iniFile << "Enabled=1\n\n";
            iniFile << "; Bass Boost gain in dB (Recommended: 3.0 to 10.0)\n";
            iniFile << "BassGainDb=6.0\n\n";
            iniFile << "; Frequency cutoff in Hz (Recommended: 60.0 to 120.0)\n";
            iniFile << "CutoffFreq=100.0\n\n";
            iniFile << "; Master Volume multiplier for debugging/hearing test (e.g. 0.5 = 50%, 1.0 = 100%, 1.5 = 150%)\n";
            iniFile << "MasterVolume=1.0\n";
            iniFile.close();
        }
    }

    // Read values from INI
    g_config.enabled = GetPrivateProfileIntA("BassBooster", "Enabled", 1, iniPath.c_str()) != 0;
    
    char buffer[32];
    GetPrivateProfileStringA("BassBooster", "BassGainDb", "6.0", buffer, sizeof(buffer), iniPath.c_str());
    g_config.bassGainDb = static_cast<float>(atof(buffer));

    GetPrivateProfileStringA("BassBooster", "CutoffFreq", "100.0", buffer, sizeof(buffer), iniPath.c_str());
    g_config.cutoffFreq = static_cast<float>(atof(buffer));

    GetPrivateProfileStringA("BassBooster", "MasterVolume", "1.0", buffer, sizeof(buffer), iniPath.c_str());
    g_config.masterVolume = static_cast<float>(atof(buffer));

    // Initialize filter for standard 44.1kHz audio stream
    g_filter.SetupLowShelf(44100.0f, g_config.cutoffFreq, g_config.bassGainDb);
}

// Function to process 16-bit PCM Audio Buffer
void ProcessAudioBuffer(short* pSamples, DWORD sampleCount, int channels) {
    if (!g_config.enabled) return;

    for (DWORD i = 0; i < sampleCount; i += channels) {
        for (int ch = 0; ch < channels && ch < 2; ++ch) {
            float sampleFloat = static_cast<float>(pSamples[i + ch]);
            
            // Apply Bass Boost Filter
            float processed = g_filter.Process(sampleFloat, ch);
            
            // Apply Debug Volume Adjuster
            processed *= g_config.masterVolume;

            // Hard Limiter / Hard Clipping Guard (-32768 to 32767)
            if (processed > 32767.0f) processed = 32767.0f;
            if (processed < -32768.0f) processed = -32768.0f;

            pSamples[i + ch] = static_cast<short>(processed);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        LoadOrGenerateConfig();
    }
    return TRUE;
}
