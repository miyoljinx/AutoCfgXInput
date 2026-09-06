#include <windows.h>
#include <objbase.h>
#include <xaudio2.h>

#include <MinHook.h>

#include <fstream>
#include <string>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cctype>

// ============================================================
// CONFIG
// ============================================================

struct Config
{
    bool enabled = true;

    float bassGainDb = 10.0f;
    float cutoffFreq = 100.0f;
    float masterVolume = 1.0f;
};

static Config g_config;


// ============================================================
// GLOBAL STATE
// ============================================================

static HMODULE g_module = nullptr;

static std::string g_gameDirectory;
static std::string g_logPath;
static std::string g_iniPath;

static std::mutex g_logMutex;

static std::atomic<bool> g_running(true);

static std::atomic<bool> g_xaudioDllDetected(false);
static std::atomic<bool> g_dllGetClassObjectHooked(false);
static std::atomic<bool> g_factoryCreateInstanceHooked(false);
static std::atomic<bool> g_createSourceVoiceHooked(false);


// ============================================================
// LOGGING
// ============================================================

static void Log(const char* format, ...)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    FILE* file =
        fopen(
            g_logPath.c_str(),
            "a"
        );

    if (!file)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    fprintf(
        file,
        "[%02u:%02u:%02u.%03u] ",
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds
    );

    va_list args;

    va_start(args, format);

    vfprintf(
        file,
        format,
        args
    );

    va_end(args);

    fprintf(
        file,
        "\n"
    );

    fclose(file);
}


// ============================================================
// PATH INITIALIZATION
// ============================================================

static void InitializePaths()
{
    char exePath[MAX_PATH] = {};

    DWORD length =
        GetModuleFileNameA(
            nullptr,
            exePath,
            MAX_PATH
        );

    if (length == 0)
    {
        g_gameDirectory = ".";
    }
    else
    {
        std::string path(exePath);

        size_t slash =
            path.find_last_of("\\/");

        if (slash != std::string::npos)
        {
            g_gameDirectory =
                path.substr(
                    0,
                    slash
                );
        }
        else
        {
            g_gameDirectory = ".";
        }
    }

    g_logPath =
        g_gameDirectory +
        "\\bass_boost.log";

    g_iniPath =
        g_gameDirectory +
        "\\bass_boost.ini";
}


// ============================================================
// CONFIGURATION
// ============================================================

static void LoadOrGenerateConfig()
{
    DWORD attrib =
        GetFileAttributesA(
            g_iniPath.c_str()
        );

    if (attrib == INVALID_FILE_ATTRIBUTES)
    {
        std::ofstream ini(
            g_iniPath
        );

        if (ini.is_open())
        {
            ini <<
                "[BassBooster]\n"
                "Enabled=1\n"
                "BassGainDb=10.0\n"
                "CutoffFreq=100.0\n"
                "MasterVolume=1.0\n";

            ini.close();

            Log(
                "Created config: %s",
                g_iniPath.c_str()
            );
        }
        else
        {
            Log(
                "ERROR: Could not create config"
            );
        }
    }

    g_config.enabled =
        GetPrivateProfileIntA(
            "BassBooster",
            "Enabled",
            1,
            g_iniPath.c_str()
        ) != 0;

    char buffer[64] = {};

    GetPrivateProfileStringA(
        "BassBooster",
        "BassGainDb",
        "10.0",
        buffer,
        sizeof(buffer),
        g_iniPath.c_str()
    );

    g_config.bassGainDb =
        static_cast<float>(
            atof(buffer)
        );

    GetPrivateProfileStringA(
        "BassBooster",
        "CutoffFreq",
        "100.0",
        buffer,
        sizeof(buffer),
        g_iniPath.c_str()
    );

    g_config.cutoffFreq =
        static_cast<float>(
            atof(buffer)
        );

    GetPrivateProfileStringA(
        "BassBooster",
        "MasterVolume",
        "1.0",
        buffer,
        sizeof(buffer),
        g_iniPath.c_str()
    );

    g_config.masterVolume =
        static_cast<float>(
            atof(buffer)
        );

    Log(
        "Configuration:"
    );

    Log(
        "  Enabled = %d",
        g_config.enabled ? 1 : 0
    );

    Log(
        "  BassGainDb = %.2f",
        g_config.bassGainDb
    );

    Log(
        "  CutoffFreq = %.2f",
        g_config.cutoffFreq
    );

    Log(
        "  MasterVolume = %.2f",
        g_config.masterVolume
    );
}


// ============================================================
// XAudio2 GUIDs
// ============================================================

// CLSID_XAudio2
static const GUID g_CLSID_XAudio2 =
{
    0x5a508685,
    0xa254,
    0x4fba,
    {
        0x9b,
        0x82,
        0x9a,
        0x24,
        0xb0,
        0x03,
        0x06,
        0xaf
    }
};


// IID_IXAudio2
static const GUID g_IID_IXAudio2 =
{
    0x8bcf1f58,
    0x9fe7,
    0x4583,
    {
        0x8a,
        0xc6,
        0xe2,
        0xad,
        0xc4,
        0x65,
        0xc8,
        0xbb
    }
};


// ============================================================
// DllGetClassObject
// ============================================================

typedef HRESULT (WINAPI* DllGetClassObject_t)(
    REFCLSID rclsid,
    REFIID riid,
    LPVOID* ppv
);

static DllGetClassObject_t
    g_originalDllGetClassObject = nullptr;


// ============================================================
// IClassFactory
// ============================================================

typedef HRESULT (STDMETHODCALLTYPE* IClassFactory_CreateInstance_t)(
    IClassFactory* This,
    IUnknown* pUnkOuter,
    REFIID riid,
    void** ppvObject
);

static IClassFactory_CreateInstance_t
    g_originalFactoryCreateInstance = nullptr;


// ============================================================
// IXAudio2
// ============================================================

typedef HRESULT (STDMETHODCALLTYPE* IXAudio2_CreateSourceVoice_t)(
    IXAudio2* This,
    IXAudio2SourceVoice** ppSourceVoice,
    const WAVEFORMATEX* pSourceFormat,
    UINT32 Flags,
    float MaxFrequencyRatio,
    IXAudio2VoiceCallback* pCallback,
    const XAUDIO2_VOICE_SENDS* pSendList,
    const XAUDIO2_EFFECT_CHAIN* pEffectChain
);

static IXAudio2_CreateSourceVoice_t
    g_originalCreateSourceVoice = nullptr;


// ============================================================
// IXAudio2SourceVoice
// ============================================================

typedef HRESULT (STDMETHODCALLTYPE* IXAudio2SourceVoice_SubmitSourceBuffer_t)(
    IXAudio2SourceVoice* This,
    const XAUDIO2_BUFFER* pBuffer,
    const XAUDIO2_BUFFER_WMA* pBufferWMA
);


// ============================================================
// TRACK SOURCE VOICES
// ============================================================

struct HookedVoice
{
    IXAudio2SourceVoice* voice;
    void* originalSubmit;
};

static std::mutex g_voiceMutex;

static std::vector<HookedVoice>
    g_hookedVoices;


// ============================================================
// SOURCE VOICE SUBMIT HOOK
// ============================================================

static HRESULT STDMETHODCALLTYPE
HookedSubmitSourceBuffer(
    IXAudio2SourceVoice* This,
    const XAUDIO2_BUFFER* pBuffer,
    const XAUDIO2_BUFFER_WMA* pBufferWMA
)
{
    if (pBuffer)
    {
        Log(
            "------------------------------------------------"
        );

        Log(
            ">>> SubmitSourceBuffer"
        );

        Log(
            "Voice = %p",
            static_cast<void*>(This)
        );

        Log(
            "AudioData = %p",
            pBuffer->pAudioData
        );

        Log(
            "AudioBytes = %u",
            pBuffer->AudioBytes
        );

        Log(
            "PlayBegin = %u",
            pBuffer->PlayBegin
        );

        Log(
            "PlayLength = %u",
            pBuffer->PlayLength
        );

        Log(
            "LoopBegin = %u",
            pBuffer->LoopBegin
        );

        Log(
            "LoopLength = %u",
            pBuffer->LoopLength
        );

        Log(
            "LoopCount = %u",
            pBuffer->LoopCount
        );

        Log(
            "Flags = 0x%08X",
            pBuffer->Flags
        );

        Log(
            "Context = %p",
            pBuffer->pContext
        );
    }


    IXAudio2SourceVoice_SubmitSourceBuffer_t original =
        nullptr;


    {
        std::lock_guard<std::mutex> lock(
            g_voiceMutex
        );


        for (
            const HookedVoice& voice :
            g_hookedVoices
        )
        {
            if (voice.voice == This)
            {
                original =
                    reinterpret_cast<
                        IXAudio2SourceVoice_SubmitSourceBuffer_t
                    >(
                        voice.originalSubmit
                    );

                break;
            }
        }
    }


    if (!original)
    {
        Log(
            "ERROR: Original SubmitSourceBuffer "
            "not found"
        );

        return E_FAIL;
    }


    HRESULT result =
        original(
            This,
            pBuffer,
            pBufferWMA
        );


    Log(
        "SubmitSourceBuffer result = 0x%08X",
        static_cast<unsigned>(result)
    );


    return result;
}


// ============================================================
// HOOK SOURCE VOICE
// ============================================================

static bool HookSourceVoice(
    IXAudio2SourceVoice* voice
)
{
    if (!voice)
        return false;


    std::lock_guard<std::mutex> lock(
        g_voiceMutex
    );


    for (
        const HookedVoice& existing :
        g_hookedVoices
    )
    {
        if (existing.voice == voice)
            return true;
    }


    void** vtable =
        *reinterpret_cast<void***>(
            voice
        );


    if (!vtable)
    {
        Log(
            "ERROR: SourceVoice vtable NULL"
        );

        return false;
    }


    // IXAudio2SourceVoice::SubmitSourceBuffer
    // is vtable entry 24.

    void* submitAddress =
        vtable[24];


    if (!submitAddress)
    {
        Log(
            "ERROR: SubmitSourceBuffer address NULL"
        );

        return false;
    }


    Log(
        "SourceVoice vtable = %p",
        static_cast<void*>(vtable)
    );


    Log(
        "SubmitSourceBuffer = %p",
        submitAddress
    );


    MH_STATUS status =
        MH_CreateHook(
            submitAddress,
            reinterpret_cast<LPVOID>(
                &HookedSubmitSourceBuffer
            ),
            nullptr
        );


    if (
        status != MH_OK &&
        status != MH_ERROR_ALREADY_CREATED
    )
    {
        Log(
            "MH_CreateHook SubmitSourceBuffer failed: %d",
            static_cast<int>(status)
        );

        return false;
    }


    status =
        MH_EnableHook(
            submitAddress
        );


    if (
        status != MH_OK &&
        status != MH_ERROR_ENABLED
    )
    {
        Log(
            "MH_EnableHook SubmitSourceBuffer failed: %d",
            static_cast<int>(status)
        );

        return false;
    }


    HookedVoice entry;

    entry.voice =
        voice;

    entry.originalSubmit =
        submitAddress;


    g_hookedVoices.push_back(
        entry
    );


    Log(
        ">>> SUCCESS: SourceVoice hooked"
    );


    Log(
        ">>> Voice = %p",
        static_cast<void*>(voice)
    );


    return true;
}


// ============================================================
// IXAudio2::CreateSourceVoice
// ============================================================

static HRESULT STDMETHODCALLTYPE
HookedCreateSourceVoice(
    IXAudio2* This,
    IXAudio2SourceVoice** ppSourceVoice,
    const WAVEFORMATEX* pSourceFormat,
    UINT32 Flags,
    float MaxFrequencyRatio,
    IXAudio2VoiceCallback* pCallback,
    const XAUDIO2_VOICE_SENDS* pSendList,
    const XAUDIO2_EFFECT_CHAIN* pEffectChain
)
{
    Log(
        "================================================"
    );

    Log(
        ">>> IXAudio2::CreateSourceVoice"
    );

    Log(
        "IXAudio2 = %p",
        static_cast<void*>(This)
    );


    if (pSourceFormat)
    {
        Log(
            "Audio format:"
        );

        Log(
            "  wFormatTag = 0x%04X",
            pSourceFormat->wFormatTag
        );

        Log(
            "  Channels = %u",
            pSourceFormat->nChannels
        );

        Log(
            "  SampleRate = %u",
            pSourceFormat->nSamplesPerSec
        );

        Log(
            "  AvgBytesPerSec = %u",
            pSourceFormat->nAvgBytesPerSec
        );

        Log(
            "  BlockAlign = %u",
            pSourceFormat->nBlockAlign
        );

        Log(
            "  BitsPerSample = %u",
            pSourceFormat->wBitsPerSample
        );

        Log(
            "  cbSize = %u",
            pSourceFormat->cbSize
        );
    }
    else
    {
        Log(
            "Audio format = NULL"
        );
    }


    Log(
        "Flags = 0x%08X",
        Flags
    );


    Log(
        "MaxFrequencyRatio = %.3f",
        MaxFrequencyRatio
    );


    Log(
        "Callback = %p",
        static_cast<void*>(pCallback)
    );


    if (!g_originalCreateSourceVoice)
    {
        Log(
            "ERROR: Original CreateSourceVoice NULL"
        );

        return E_FAIL;
    }


    HRESULT result =
        g_originalCreateSourceVoice(
            This,
            ppSourceVoice,
            pSourceFormat,
            Flags,
            MaxFrequencyRatio,
            pCallback,
            pSendList,
            pEffectChain
        );


    Log(
        "CreateSourceVoice result = 0x%08X",
        static_cast<unsigned>(result)
    );


    if (
        SUCCEEDED(result) &&
        ppSourceVoice &&
        *ppSourceVoice
    )
    {
        Log(
            ">>> NEW SourceVoice = %p",
            static_cast<void*>(*ppSourceVoice)
        );


        HookSourceVoice(
            *ppSourceVoice
        );
    }


    Log(
        "================================================"
    );


    return result;
}


// ============================================================
// HOOK IXAudio2
// ============================================================

static bool HookIXAudio2(
    IXAudio2* audio
)
{
    if (!audio)
        return false;


    if (g_createSourceVoiceHooked)
    {
        Log(
            "IXAudio2 CreateSourceVoice already hooked"
        );

        return true;
    }


    void** vtable =
        *reinterpret_cast<void***>(
            audio
        );


    if (!vtable)
    {
        Log(
            "ERROR: IXAudio2 vtable NULL"
        );

        return false;
    }


    void* createSourceVoice =
        vtable[8];


    if (!createSourceVoice)
    {
        Log(
            "ERROR: CreateSourceVoice address NULL"
        );

        return false;
    }


    Log(
        "IXAudio2 vtable = %p",
        static_cast<void*>(vtable)
    );


    Log(
        "CreateSourceVoice = %p",
        createSourceVoice
    );


    MH_STATUS status =
        MH_CreateHook(
            createSourceVoice,
            reinterpret_cast<LPVOID>(
                &HookedCreateSourceVoice
            ),
            reinterpret_cast<LPVOID*>(
                &g_originalCreateSourceVoice
            )
        );


    if (
        status != MH_OK &&
        status != MH_ERROR_ALREADY_CREATED
    )
    {
        Log(
            "MH_CreateHook CreateSourceVoice failed: %d",
            static_cast<int>(status)
        );

        return false;
    }


    status =
        MH_EnableHook(
            createSourceVoice
        );


    if (
        status != MH_OK &&
        status != MH_ERROR_ENABLED
    )
    {
        Log(
            "MH_EnableHook CreateSourceVoice failed: %d",
            static_cast<int>(status)
        );

        return false;
    }


    g_createSourceVoiceHooked = true;


    Log(
        ">>> SUCCESS: IXAudio2::CreateSourceVoice HOOKED"
    );


    return true;
}


// ============================================================
// IClassFactory::CreateInstance HOOK
// ============================================================

static HRESULT STDMETHODCALLTYPE
HookedFactoryCreateInstance(
    IClassFactory* This,
    IUnknown* pUnkOuter,
    REFIID riid,
    void** ppvObject
)
{
    Log(
        "------------------------------------------------"
    );

    Log(
        ">>> XAudio2 IClassFactory::CreateInstance"
    );

    Log(
        "Factory = %p",
        static_cast<void*>(This)
    );


    Log(
        "riid = %08lX-%04X-%04X-...",
        riid.Data1,
        riid.Data2,
        riid.Data3
    );


    HRESULT result =
        g_originalFactoryCreateInstance(
            This,
            pUnkOuter,
            riid,
            ppvObject
        );


    Log(
        "Factory CreateInstance result = 0x%08X",
        static_cast<unsigned>(result)
    );


    if (
        SUCCEEDED(result) &&
        ppvObject &&
        *ppvObject
    )
    {
        Log(
            "Factory returned object = %p",
            *ppvObject
        );


        if (
            IsEqualGUID(
                riid,
                g_IID_IXAudio2
            )
        )
        {
            IXAudio2* audio =
                reinterpret_cast<IXAudio2*>(
                    *ppvObject
                );


            Log(
                ">>> IXAudio2 interface received"
            );


            HookIXAudio2(
                audio
            );
        }
        else
        {
            Log(
                "Returned interface is not IXAudio2"
            );
        }
    }


    return result;
}


// ============================================================
// HOOK IClassFactory
// ============================================================

static bool HookClassFactory(
    IClassFactory* factory
)
{
    if (!factory)
        return false;


    if (g_factoryCreateInstanceHooked)
    {
        return true;
    }


    void** vtable =
        *reinterpret_cast<void***>(
            factory
        );


    if (!vtable)
    {
        Log(
            "ERROR: Factory vtable NULL"
        );

        return false;
    }


    // IUnknown:
    //
    // 0 QueryInterface
    // 1 AddRef
    // 2 Release
    //
    // IClassFactory:
    //
    // 3 CreateInstance
    // 4 LockServer

    void* createInstance =
        vtable[3];


    if (!createInstance)
    {
        Log(
            "ERROR: Factory CreateInstance NULL"
        );

        return false;
    }


    Log(
        "IClassFactory vtable = %p",
        static_cast<void*>(vtable)
    );


    Log(
        "IClassFactory::CreateInstance = %p",
        createInstance
    );


    MH_STATUS status =
        MH_CreateHook(
            createInstance,
            reinterpret_cast<LPVOID>(
                &HookedFactoryCreateInstance
            ),
            reinterpret_cast<LPVOID*>(
                &g_originalFactoryCreateInstance
            )
        );


    if (
        status != MH_OK &&
        status != MH_ERROR_ALREADY_CREATED
    )
    {
        Log(
            "MH_CreateHook Factory CreateInstance failed: %d",
            static_cast<int>(status)
        );

        return false;
    }


    status =
        MH_EnableHook(
            createInstance
        );


    if (
        status != MH_OK &&
        status != MH_ERROR_ENABLED
    )
    {
        Log(
            "MH_EnableHook Factory CreateInstance failed: %d",
            static_cast<int>(status)
        );

        return false;
    }


    g_factoryCreateInstanceHooked = true;


    Log(
        ">>> SUCCESS: XAudio2 IClassFactory::CreateInstance HOOKED"
    );


    return true;
}


// ============================================================
// DllGetClassObject HOOK
// ============================================================

static HRESULT WINAPI
HookedDllGetClassObject(
    REFCLSID rclsid,
    REFIID riid,
    LPVOID* ppv
)
{
    bool isXAudioClass =
        IsEqualGUID(
            rclsid,
            g_CLSID_XAudio2
        );


    if (isXAudioClass)
    {
        Log(
            "================================================"
        );

        Log(
            ">>> xaudio2_7.dll DllGetClassObject"
        );

        Log(
            ">>> XAudio2 CLSID requested"
        );
    }


    if (!g_originalDllGetClassObject)
    {
        Log(
            "ERROR: Original DllGetClassObject NULL"
        );

        return E_FAIL;
    }


    HRESULT result =
        g_originalDllGetClassObject(
            rclsid,
            riid,
            ppv
        );


    if (isXAudioClass)
    {
        Log(
            "DllGetClassObject result = 0x%08X",
            static_cast<unsigned>(result)
        );


        if (
            SUCCEEDED(result) &&
            ppv &&
            *ppv
        )
        {
            Log(
                "Class factory object = %p",
                *ppv
            );


            IClassFactory* factory =
                reinterpret_cast<IClassFactory*>(
                    *ppv
                );


            HookClassFactory(
                factory
            );
        }
        else
        {
            Log(
                ">>> DllGetClassObject returned no factory"
            );
        }


        Log(
            "================================================"
        );
    }


    return result;
}


// ============================================================
// INSTALL DllGetClassObject HOOK
// ============================================================

static bool InstallDllGetClassObjectHook()
{
    if (g_dllGetClassObjectHooked)
        return true;


    HMODULE xaudio =
        GetModuleHandleA(
            "xaudio2_7.dll"
        );


    if (!xaudio)
    {
        Log(
            "xaudio2_7.dll not loaded"
        );

        return false;
    }


    Log(
        ">>> xaudio2_7.dll module = %p",
        static_cast<void*>(xaudio)
    );


    FARPROC proc =
        GetProcAddress(
            xaudio,
            "DllGetClassObject"
        );


    if (!proc)
    {
        Log(
            "ERROR: xaudio2_7.dll does NOT export DllGetClassObject"
        );

        return false;
    }


    Log(
        "DllGetClassObject = %p",
        reinterpret_cast<void*>(proc)
    );


    MH_STATUS status =
        MH_CreateHook(
            reinterpret_cast<LPVOID>(
                proc
            ),
            reinterpret_cast<LPVOID>(
                &HookedDllGetClassObject
            ),
            reinterpret_cast<LPVOID*>(
                &g_originalDllGetClassObject
            )
        );


    Log(
        "DllGetClassObject MH_CreateHook = %d",
        static_cast<int>(status)
    );


    if (
        status != MH_OK &&
        status != MH_ERROR_ALREADY_CREATED
    )
    {
        return false;
    }


    status =
        MH_EnableHook(
            reinterpret_cast<LPVOID>(
                proc
            )
        );


    Log(
        "DllGetClassObject MH_EnableHook = %d",
        static_cast<int>(status)
    );


    if (
        status != MH_OK &&
        status != MH_ERROR_ENABLED
    )
    {
        return false;
    }


    g_dllGetClassObjectHooked = true;


    Log(
        ">>> SUCCESS: xaudio2_7.dll DllGetClassObject HOOKED"
    );


    return true;
}


// ============================================================
// XAudio DLL DETECTION
// ============================================================

static void CheckXAudioModule()
{
    if (g_dllGetClassObjectHooked)
        return;


    HMODULE xaudio =
        GetModuleHandleA(
            "xaudio2_7.dll"
        );


    if (!xaudio)
        return;


    if (!g_xaudioDllDetected)
    {
        g_xaudioDllDetected = true;


        Log(
            "================================================"
        );


        Log(
            ">>> XAudio2_7 DLL DETECTED"
        );


        Log(
            "Module = %p",
            static_cast<void*>(xaudio)
        );


        Log(
            "Attempting LOCAL DllGetClassObject hook..."
        );


        InstallDllGetClassObjectHook();


        Log(
            "================================================"
        );
    }
}


// ============================================================
// LoadLibraryA MONITOR
// ============================================================

typedef HMODULE (WINAPI* LoadLibraryA_t)(
    LPCSTR
);

static LoadLibraryA_t
    g_originalLoadLibraryA = nullptr;


static bool IsXAudioName(
    const char* name
)
{
    if (!name)
        return false;


    std::string s(name);


    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(
                std::tolower(c)
            );
        }
    );


    return
        s.find("xaudio2_7.dll") !=
        std::string::npos;
}


static HMODULE WINAPI
HookedLoadLibraryA(
    LPCSTR lpLibFileName
)
{
    if (
        lpLibFileName &&
        IsXAudioName(lpLibFileName)
    )
    {
        Log(
            ">>> LoadLibraryA REQUEST: %s",
            lpLibFileName
        );
    }


    HMODULE module =
        g_originalLoadLibraryA(
            lpLibFileName
        );


    if (
        lpLibFileName &&
        IsXAudioName(lpLibFileName)
    )
    {
        Log(
            ">>> LoadLibraryA RESULT: %p",
            static_cast<void*>(module)
        );


        if (module)
        {
            Sleep(5);

            CheckXAudioModule();
        }
    }


    return module;
}


// ============================================================
// INSTALL LoadLibraryA HOOK
// ============================================================

static void InstallLoadLibraryHook()
{
    HMODULE kernel32 =
        GetModuleHandleA(
            "kernel32.dll"
        );


    if (!kernel32)
    {
        Log(
            "ERROR: kernel32.dll not found"
        );

        return;
    }


    FARPROC proc =
        GetProcAddress(
            kernel32,
            "LoadLibraryA"
        );


    if (!proc)
    {
        Log(
            "ERROR: LoadLibraryA not found"
        );

        return;
    }


    MH_STATUS status =
        MH_CreateHook(
            reinterpret_cast<LPVOID>(
                proc
            ),
            reinterpret_cast<LPVOID>(
                &HookedLoadLibraryA
            ),
            reinterpret_cast<LPVOID*>(
                &g_originalLoadLibraryA
            )
        );


    Log(
        "LoadLibraryA hook create = %d",
        static_cast<int>(status)
    );


    if (status == MH_OK)
    {
        status =
            MH_EnableHook(
                reinterpret_cast<LPVOID>(
                    proc
                )
            );


        Log(
            "LoadLibraryA hook enable = %d",
            static_cast<int>(status)
        );
    }
}


// ============================================================
// BACKGROUND SCANNER
// ============================================================

static DWORD WINAPI
ScannerThread(
    LPVOID
)
{
    Log(
        "XAudio module scanner started."
    );


    while (g_running)
    {
        CheckXAudioModule();

        Sleep(100);
    }


    Log(
        "XAudio module scanner stopped."
    );


    return 0;
}


// ============================================================
// INITIALIZATION
// ============================================================

static DWORD WINAPI
InitThread(
    LPVOID
)
{
    InitializePaths();


    DeleteFileA(
        g_logPath.c_str()
    );


    Log(
        "================================================"
    );

    Log(
        "       BASS BOOST ASI STARTING"
    );

    Log(
        "================================================"
    );


    Log(
        "Game directory:"
    );

    Log(
        "%s",
        g_gameDirectory.c_str()
    );


    Log(
        "Log:"
    );

    Log(
        "%s",
        g_logPath.c_str()
    );


    Log(
        "Config:"
    );

    Log(
        "%s",
        g_iniPath.c_str()
    );


    LoadOrGenerateConfig();


    // --------------------------------------------------------
    // MinHook
    // --------------------------------------------------------

    Log(
        "Initializing MinHook..."
    );


    MH_STATUS status =
        MH_Initialize();


    Log(
        "MH_Initialize = %d",
        static_cast<int>(status)
    );


    if (
        status != MH_OK &&
        status != MH_ERROR_ALREADY_INITIALIZED
    )
    {
        Log(
            "ERROR: MinHook initialization failed"
        );

        return 0;
    }


    // --------------------------------------------------------
    // Install LoadLibrary monitor
    // --------------------------------------------------------

    InstallLoadLibraryHook();


    // --------------------------------------------------------
    // Check immediately
    // --------------------------------------------------------

    CheckXAudioModule();


    // --------------------------------------------------------
    // Background scanner
    // --------------------------------------------------------

    HANDLE scanner =
        CreateThread(
            nullptr,
            0,
            ScannerThread,
            nullptr,
            0,
            nullptr
        );


    if (scanner)
    {
        CloseHandle(scanner);
    }
    else
    {
        Log(
            "ERROR: Could not create scanner thread"
        );
    }


    Log(
        "================================================"
    );

    Log(
        "Initialization complete."
    );

    Log(
        "Waiting for Wine xaudio2_7 COM factory..."
    );

    Log(
        "CoCreateInstance is NOT hooked."
    );

    Log(
        "================================================"
    );


    return 0;
}


// ============================================================
// DLL MAIN
// ============================================================

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID
)
{
    if (
        reason ==
        DLL_PROCESS_ATTACH
    )
    {
        g_module =
            hModule;


        DisableThreadLibraryCalls(
            hModule
        );


        HANDLE thread =
            CreateThread(
                nullptr,
                0,
                InitThread,
                nullptr,
                0,
                nullptr
            );


        if (thread)
        {
            CloseHandle(thread);
        }
    }


    else if (
        reason ==
        DLL_PROCESS_DETACH
    )
    {
        g_running = false;

        MH_Uninitialize();
    }


    return TRUE;
}