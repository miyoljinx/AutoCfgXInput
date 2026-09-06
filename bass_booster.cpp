#include <windows.h>
#include <xaudio2.h>

#include <MinHook.h>

#include <fstream>
#include <string>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <cmath>

// ============================================================
// CONFIG
// ============================================================

struct Config {
bool enabled = true;

float bassGainDb = 10.0f;
float cutoffFreq = 100.0f;

// 1.0 = original volume
// Keep this at 1.0 for initial testing.
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
std::lock_guard"std::mutex" (std::mutex) lock(g_logMutex);

FILE* file = nullptr;

fopen_s(&file, g_logPath.c_str(), "a");

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
// GAME DIRECTORY
// ============================================================

void InitializePaths()
{
char exePath[MAX_PATH] = {};

GetModuleFileNameA(
    nullptr,
    exePath,
    MAX_PATH
);

std::string path = exePath;

size_t slash = path.find_last_of("\\/");

if (slash != std::string::npos)
    g_gameDirectory = path.substr(0, slash);
else
    g_gameDirectory = ".";

g_logPath =
    g_gameDirectory +
    "\\bass_boost.log";

g_iniPath =
    g_gameDirectory +
    "\\bass_boost.ini";

}

// ============================================================
// CONFIG
// ============================================================

void LoadOrGenerateConfig()
{
DWORD attrib =
GetFileAttributesA(
g_iniPath.c_str()
);

if (attrib == INVALID_FILE_ATTRIBUTES)
{
    std::ofstream ini(g_iniPath);

    if (ini.is_open())
    {
        ini <<
            "[BassBooster]\n"
            "Enabled=1\n"
            "BassGainDb=10.0\n"
            "CutoffFreq=100.0\n"
            "MasterVolume=1.0\n";

        ini.close();
    }

    Log(
        "Created config: %s",
        g_iniPath.c_str()
    );
}

g_config.enabled =
    GetPrivateProfileIntA(
        "BassBooster",
        "Enabled",
        1,
        g_iniPath.c_str()
    ) != 0;


char buffer[64];


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
    "Config loaded:"
);

Log(
    "Enabled=%d",
    g_config.enabled
);

Log(
    "BassGainDb=%.2f",
    g_config.bassGainDb
);

Log(
    "CutoffFreq=%.2f",
    g_config.cutoffFreq
);

Log(
    "MasterVolume=%.2f",
    g_config.masterVolume
);

}

// ============================================================
// XAudio2Create HOOK
// ============================================================

typedef HRESULT (WINAPI* XAudio2Create_t)(
IXAudio2** ppXAudio2,
UINT32 Flags,
XAUDIO2_PROCESSOR XAudio2Processor
);

static XAudio2Create_t
g_originalXAudio2Create = nullptr;

// ============================================================
// HOOK
// ============================================================

HRESULT WINAPI HookedXAudio2Create(
IXAudio2** ppXAudio2,
UINT32 Flags,
XAUDIO2_PROCESSOR XAudio2Processor
)
{
Log(
">>> XAudio2Create CALLED"
);

Log(
    "Flags = %u",
    Flags
);

Log(
    "Processor = %u",
    static_cast<unsigned>(XAudio2Processor)
);


HRESULT result =
    g_originalXAudio2Create(
        ppXAudio2,
        Flags,
        XAudio2Processor
    );


Log(
    "XAudio2Create result = 0x%08X",
    static_cast<unsigned>(result)
);


if (SUCCEEDED(result) &&
    ppXAudio2 &&
    *ppXAudio2)
{
    Log(
        ">>> IXAudio2 OBJECT CREATED: %p",
        *ppXAudio2
    );

    Log(
        "Next stage required: "
        "hook CreateSourceVoice on this IXAudio2 instance"
    );
}
else
{
    Log(
        "XAudio2Create failed or returned null object"
    );
}


return result;

}

// ============================================================
// XAudio DLL DETECTION
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
// INSTALL XAudio HOOK
// ============================================================

bool TryHookXAudioDLL(
const char* dllName
)
{
HMODULE module =
GetModuleHandleA(
dllName
);

if (!module)
{
    Log(
        "%s not currently loaded",
        dllName
    );

    return false;
}


Log(
    "%s detected at %p",
    dllName,
    module
);


FARPROC proc =
    GetProcAddress(
        module,
        "XAudio2Create"
    );


if (!proc)
{
    Log(
        "%s loaded but XAudio2Create export not found",
        dllName
    );

    return false;
}


Log(
    "XAudio2Create found in %s at %p",
    dllName,
    proc
);


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
        "MH_CreateHook failed for %s. Status=%d",
        dllName,
        static_cast<int>(status)
    );

    return false;
}


status =
    MH_EnableHook(
        reinterpret_cast<LPVOID>(proc)
    );


if (status != MH_OK)
{
    Log(
        "MH_EnableHook failed for %s. Status=%d",
        dllName,
        static_cast<int>(status)
    );

    return false;
}


Log(
    "SUCCESS: XAudio2Create hook installed for %s",
    dllName
);


return true;

}

// ============================================================
// INITIALIZE XAudio HOOK
// ============================================================

void InitializeXAudioHooks()
{
Log(
"================================================"
);

Log(
    "Scanning for XAudio DLLs..."
);


bool hooked = false;


for (const auto& candidate :
     g_xaudioCandidates)
{
    if (TryHookXAudioDLL(
            candidate.dllName
        ))
    {
        hooked = true;
    }
}


if (!hooked)
{
    Log(
        "No XAudio DLL currently loaded."
    );

    Log(
        "IMPORTANT:"
    );

    Log(
        "Game may load XAudio later."
    );

    Log(
        "This diagnostic version currently only hooks "
        "DLLs already loaded at ASI initialization."
    );
}


Log(
    "================================================"
);

}

// ============================================================
// THREAD
// ============================================================

DWORD WINAPI InitThread(
LPVOID
)
{
InitializePaths();

// Fresh log every launch.

DeleteFileA(
    g_logPath.c_str()
);


Log(
    "Bass Boost ASI started"
);

Log(
    "Game directory: %s",
    g_gameDirectory.c_str()
);


LoadOrGenerateConfig();


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
        "MinHook initialization failed"
    );

    return 0;
}


InitializeXAudioHooks();


Log(
    "Initialization finished"
);


return 0;

}

// ============================================================
// DLL ENTRY
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


else if (reason ==
         DLL_PROCESS_DETACH)
{
    MH_Uninitialize();
}


return TRUE;

}