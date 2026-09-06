#include <windows.h>
#include <xaudio2.h>

#include <MinHook.h>

#include <fstream>
#include <string>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

// ============================================================
// CONFIG
// ============================================================

struct Config {
    bool enabled = true;

    float bassGainDb = 10.0f;
    float cutoffFreq = 100.0f;

    // 1.0 = original volume
    // 0.2 = 20%
    float masterVolume = 1.0f;

} g_config;


// ============================================================
// GLOBALS
// ============================================================

static HMODULE g_module = nullptr;

static std::string g_gameDirectory;
static std::string g_logPath;
static std::string g_iniPath;

static std::mutex g_logMutex;


// ============================================================
// LOGGING
// ============================================================

void Log(const char* format, ...)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    FILE* file = fopen(g_logPath.c_str(), "a");

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

    vfprintf(file, format, args);

    va_end(args);

    fprintf(file, "\n");

    fclose(file);
}


// ============================================================
// PATH INITIALIZATION
// ============================================================

void InitializePaths()
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
                path.substr(0, slash);
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

void LoadOrGenerateConfig()
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
                "ERROR: Could not create config: %s",
                g_iniPath.c_str()
            );
        }
    }


    // --------------------------------------------------------
    // Enabled
    // --------------------------------------------------------

    g_config.enabled =
        GetPrivateProfileIntA(
            "BassBooster",
            "Enabled",
            1,
            g_iniPath.c_str()
        ) != 0;


    // --------------------------------------------------------
    // Bass Gain
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // Cutoff Frequency
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // Master Volume
    // --------------------------------------------------------

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


    // --------------------------------------------------------
    // Log configuration
    // --------------------------------------------------------

    Log(
        "Configuration loaded"
    );

    Log(
        "Enabled = %d",
        g_config.enabled ? 1 : 0
    );

    Log(
        "BassGainDb = %.2f",
        g_config.bassGainDb
    );

    Log(
        "CutoffFreq = %.2f",
        g_config.cutoffFreq
    );

    Log(
        "MasterVolume = %.2f",
        g_config.masterVolume
    );
}


// ============================================================
// XAudio2Create FUNCTION TYPE
// ============================================================

typedef HRESULT (WINAPI* XAudio2Create_t)(
    IXAudio2** ppXAudio2,
    UINT32 Flags,
    XAUDIO2_PROCESSOR XAudio2Processor
);


// ============================================================
// ORIGINAL FUNCTION POINTER
// ============================================================

static XAudio2Create_t
    g_originalXAudio2Create = nullptr;


// ============================================================
// HOOKED XAudio2Create
// ============================================================

HRESULT WINAPI HookedXAudio2Create(
    IXAudio2** ppXAudio2,
    UINT32 Flags,
    XAUDIO2_PROCESSOR XAudio2Processor
)
{
    Log(
        "=============================================="
    );

    Log(
        ">>> XAudio2Create CALLED"
    );

    Log(
        "Flags = %u",
        Flags
    );

    Log(
        "Processor = %u",
        static_cast<unsigned>(
            XAudio2Processor
        )
    );


    if (!g_originalXAudio2Create)
    {
        Log(
            "ERROR: Original XAudio2Create pointer is NULL"
        );

        return E_FAIL;
    }


    HRESULT result =
        g_originalXAudio2Create(
            ppXAudio2,
            Flags,
            XAudio2Processor
        );


    Log(
        "XAudio2Create result = 0x%08X",
        static_cast<unsigned>(
            result
        )
    );


    if (SUCCEEDED(result) &&
        ppXAudio2 &&
        *ppXAudio2)
    {
        Log(
            ">>> IXAudio2 OBJECT CREATED: %p",
            static_cast<void*>(*ppXAudio2)
        );

        Log(
            "Next stage:"
        );

        Log(
            "Hook IXAudio2::CreateSourceVoice"
        );

        Log(
            "Then hook IXAudio2SourceVoice::SubmitSourceBuffer"
        );
    }
    else
    {
        Log(
            "XAudio2Create returned no valid IXAudio2 object"
        );
    }


    Log(
        "=============================================="
    );


    return result;
}


// ============================================================
// XAudio DLL CANDIDATES
// ============================================================

struct XAudioCandidate
{
    const char* dllName;
};


static const XAudioCandidate
g_xaudioCandidates[] =
{
    { "xaudio2_9.dll" },
    { "xaudio2_8.dll" },
    { "xaudio2_7.dll" },
    { "xaudio2.dll" }
};


// ============================================================
// TRY TO HOOK ONE XAudio DLL
// ============================================================

bool TryHookXAudioDLL(
    const char* dllName
)
{
    Log(
        "Checking %s...",
        dllName
    );


    HMODULE module =
        GetModuleHandleA(
            dllName
        );


    if (!module)
    {
        Log(
            "%s is NOT currently loaded",
            dllName
        );

        return false;
    }


    Log(
        "%s detected at %p",
        dllName,
        static_cast<void*>(module)
    );


    FARPROC proc =
        GetProcAddress(
            module,
            "XAudio2Create"
        );


    if (!proc)
    {
        Log(
            "%s is loaded but XAudio2Create "
            "export was NOT found",
            dllName
        );

        return false;
    }


    Log(
        "XAudio2Create found in %s at %p",
        dllName,
        reinterpret_cast<void*>(proc)
    );


    // --------------------------------------------------------
    // Create MinHook hook
    // --------------------------------------------------------

    MH_STATUS status =
        MH_CreateHook(
            reinterpret_cast<LPVOID>(proc),

            reinterpret_cast<LPVOID>(
                &HookedXAudio2Create
            ),

            reinterpret_cast<LPVOID*>(
                &g_originalXAudio2Create
            )
        );


    if (status != MH_OK)
    {
        Log(
            "MH_CreateHook FAILED for %s",
            dllName
        );

        Log(
            "MinHook status = %d",
            static_cast<int>(status)
        );

        return false;
    }


    Log(
        "MH_CreateHook SUCCESS for %s",
        dllName
    );


    // --------------------------------------------------------
    // Enable hook
    // --------------------------------------------------------

    status =
        MH_EnableHook(
            reinterpret_cast<LPVOID>(proc)
        );


    if (status != MH_OK)
    {
        Log(
            "MH_EnableHook FAILED for %s",
            dllName
        );

        Log(
            "MinHook status = %d",
            static_cast<int>(status)
        );

        return false;
    }


    Log(
        "MH_EnableHook SUCCESS for %s",
        dllName
    );


    Log(
        ">>> XAudio2Create HOOK INSTALLED"
    );


    return true;
}


// ============================================================
// INITIALIZE XAudio HOOKS
// ============================================================

void InitializeXAudioHooks()
{
    Log(
        "=============================================="
    );

    Log(
        "Scanning for XAudio DLLs..."
    );


    bool hooked = false;


    const size_t count =
        sizeof(g_xaudioCandidates) /
        sizeof(g_xaudioCandidates[0]);


    for (size_t i = 0; i < count; ++i)
    {
        if (TryHookXAudioDLL(
                g_xaudioCandidates[i].dllName
            ))
        {
            hooked = true;
        }
    }


    if (hooked)
    {
        Log(
            "At least one XAudio2Create hook "
            "was successfully installed."
        );
    }
    else
    {
        Log(
            "NO XAudio2Create hooks installed."
        );

        Log(
            "XAudio DLL may be loaded later by the game."
        );
    }


    Log(
        "=============================================="
    );
}


// ============================================================
// INITIALIZATION THREAD
// ============================================================

DWORD WINAPI InitThread(
    LPVOID
)
{
    // --------------------------------------------------------
    // Determine game directory first
    // --------------------------------------------------------

    InitializePaths();


    // --------------------------------------------------------
    // Start a fresh log
    // --------------------------------------------------------

    DeleteFileA(
        g_logPath.c_str()
    );


    Log(
        "================================================"
    );

    Log(
        "        BASS BOOST ASI STARTING"
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
        "Log file:"
    );

    Log(
        "%s",
        g_logPath.c_str()
    );


    Log(
        "Config file:"
    );

    Log(
        "%s",
        g_iniPath.c_str()
    );


    // --------------------------------------------------------
    // Configuration
    // --------------------------------------------------------

    LoadOrGenerateConfig();


    // --------------------------------------------------------
    // Initialize MinHook
    // --------------------------------------------------------

    Log(
        "Initializing MinHook..."
    );


    MH_STATUS status =
        MH_Initialize();


    Log(
        "MH_Initialize result = %d",
        static_cast<int>(status)
    );


    if (status != MH_OK &&
        status != MH_ERROR_ALREADY_INITIALIZED)
    {
        Log(
            "ERROR: MinHook initialization failed."
        );

        return 0;
    }


    Log(
        "MinHook initialized successfully."
    );


    // --------------------------------------------------------
    // XAudio
    // --------------------------------------------------------

    InitializeXAudioHooks();


    // --------------------------------------------------------
    // Finished
    // --------------------------------------------------------

    Log(
        "================================================"
    );

    Log(
        "Bass Boost ASI initialization finished."
    );

    Log(
        "Waiting for XAudio activity..."
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
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = hModule;


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
            CloseHandle(
                thread
            );
        }
    }


    else if (reason == DLL_PROCESS_DETACH)
    {
        MH_Uninitialize();
    }


    return TRUE;
}