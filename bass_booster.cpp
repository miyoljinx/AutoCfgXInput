#include <windows.h>
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
#include <algorithm>

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

static std::atomic<bool> g_xaudio27Hooked(false);
static std::atomic<bool> g_xaudio28Hooked(false);
static std::atomic<bool> g_xaudio29Hooked(false);
static std::atomic<bool> g_xaudioHooked(false);

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
// INITIALIZE PATHS
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
// CONFIG
// ============================================================

static void LoadOrGenerateConfig()
{
DWORD attrib =
GetFileAttributesA(
g_iniPath.c_str()
);

if (attrib ==
    INVALID_FILE_ATTRIBUTES)
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
// XAudio2Create TYPE
// ============================================================

typedef HRESULT (WINAPI* XAudio2Create_t)(
IXAudio2**,
UINT32,
XAUDIO2_PROCESSOR
);

// ============================================================
// ORIGINAL XAudio2Create
// ============================================================

static XAudio2Create_t
g_originalXAudio2Create27 = nullptr;

static XAudio2Create_t
g_originalXAudio2Create28 = nullptr;

static XAudio2Create_t
g_originalXAudio2Create29 = nullptr;

// ============================================================
// XAudio2Create HOOK
// ============================================================

static HRESULT WINAPI
HookedXAudio2Create27(
IXAudio2** ppXAudio2,
UINT32 Flags,
XAUDIO2_PROCESSOR Processor
)
{
Log(
"=============================================="
);

Log(
    ">>> XAUDIO2_7: XAudio2Create CALLED"
);

Log(
    "Flags = %u",
    Flags
);

Log(
    "Processor = %u",
    static_cast<unsigned>(
        Processor
    )
);


if (!g_originalXAudio2Create27)
{
    Log(
        "ERROR: original XAudio2Create 2.7 is NULL"
    );

    return E_FAIL;
}


HRESULT result =
    g_originalXAudio2Create27(
        ppXAudio2,
        Flags,
        Processor
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
        ">>> IXAudio2 CREATED: %p",
        static_cast<void*>(*ppXAudio2)
    );
}
else
{
    Log(
        ">>> IXAudio2 creation FAILED"
    );
}


Log(
    "=============================================="
);


return result;

}

static HRESULT WINAPI
HookedXAudio2Create28(
IXAudio2** ppXAudio2,
UINT32 Flags,
XAUDIO2_PROCESSOR Processor
)
{
Log(
"=============================================="
);

Log(
    ">>> XAUDIO2_8: XAudio2Create CALLED"
);


Log(
    "Flags = %u",
    Flags
);


Log(
    "Processor = %u",
    static_cast<unsigned>(
        Processor
    )
);


if (!g_originalXAudio2Create28)
{
    Log(
        "ERROR: original XAudio2Create 2.8 is NULL"
    );

    return E_FAIL;
}


HRESULT result =
    g_originalXAudio2Create28(
        ppXAudio2,
        Flags,
        Processor
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
        ">>> IXAudio2 CREATED: %p",
        static_cast<void*>(*ppXAudio2)
    );
}


Log(
    "=============================================="
);


return result;

}

static HRESULT WINAPI
HookedXAudio2Create29(
IXAudio2** ppXAudio2,
UINT32 Flags,
XAUDIO2_PROCESSOR Processor
)
{
Log(
"=============================================="
);

Log(
    ">>> XAUDIO2_9: XAudio2Create CALLED"
);


Log(
    "Flags = %u",
    Flags
);


Log(
    "Processor = %u",
    static_cast<unsigned>(
        Processor
    )
);


if (!g_originalXAudio2Create29)
{
    Log(
        "ERROR: original XAudio2Create 2.9 is NULL"
    );

    return E_FAIL;
}


HRESULT result =
    g_originalXAudio2Create29(
        ppXAudio2,
        Flags,
        Processor
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
        ">>> IXAudio2 CREATED: %p",
        static_cast<void*>(*ppXAudio2)
    );
}


Log(
    "=============================================="
);


return result;

}

// ============================================================
// INSTALL XAudio2Create HOOK
// ============================================================

static bool InstallXAudioHook(
const char* dllName
)
{
HMODULE module =
GetModuleHandleA(
dllName
);

if (!module)
    return false;


FARPROC proc =
    GetProcAddress(
        module,
        "XAudio2Create"
    );


if (!proc)
{
    Log(
        "%s loaded but XAudio2Create "
        "was not exported",
        dllName
    );

    return false;
}


Log(
    "XAudio2Create found:"
);


Log(
    "  DLL = %s",
    dllName
);


Log(
    "  Module = %p",
    static_cast<void*>(module)
);


Log(
    "  Function = %p",
    reinterpret_cast<void*>(proc)
);


LPVOID detour = nullptr;
LPVOID* original = nullptr;


if (_stricmp(
        dllName,
        "xaudio2_7.dll"
    ) == 0)
{
    if (g_xaudio27Hooked)
        return true;


    detour =
        reinterpret_cast<LPVOID>(
            &HookedXAudio2Create27
        );


    original =
        reinterpret_cast<LPVOID*>(
            &g_originalXAudio2Create27
        );
}


else if (_stricmp(
             dllName,
             "xaudio2_8.dll"
         ) == 0)
{
    if (g_xaudio28Hooked)
        return true;


    detour =
        reinterpret_cast<LPVOID>(
            &HookedXAudio2Create28
        );


    original =
        reinterpret_cast<LPVOID*>(
            &g_originalXAudio2Create28
        );
}


else if (_stricmp(
             dllName,
             "xaudio2_9.dll"
         ) == 0)
{
    if (g_xaudio29Hooked)
        return true;


    detour =
        reinterpret_cast<LPVOID>(
            &HookedXAudio2Create29
        );


    original =
        reinterpret_cast<LPVOID*>(
            &g_originalXAudio2Create29
        );
}


else
{
    Log(
        "Unknown XAudio DLL: %s",
        dllName
    );

    return false;
}


MH_STATUS status =
    MH_CreateHook(
        reinterpret_cast<LPVOID>(
            proc
        ),
        detour,
        original
    );


if (status != MH_OK)
{
    Log(
        "MH_CreateHook FAILED"
    );

    Log(
        "Status = %d",
        static_cast<int>(status)
    );

    return false;
}


status =
    MH_EnableHook(
        reinterpret_cast<LPVOID>(
            proc
        )
    );


if (status != MH_OK)
{
    Log(
        "MH_EnableHook FAILED"
    );

    Log(
        "Status = %d",
        static_cast<int>(status)
    );

    return false;
}


if (_stricmp(
        dllName,
        "xaudio2_7.dll"
    ) == 0)
{
    g_xaudio27Hooked = true;
}


if (_stricmp(
        dllName,
        "xaudio2_8.dll"
    ) == 0)
{
    g_xaudio28Hooked = true;
}


if (_stricmp(
        dllName,
        "xaudio2_9.dll"
    ) == 0)
{
    g_xaudio29Hooked = true;
}


g_xaudioHooked = true;


Log(
    ">>> SUCCESS: %s XAudio2Create HOOK INSTALLED",
    dllName
);


return true;

}

// ============================================================
// SCAN ALL XAudio DLLS
// ============================================================

static void ScanForXAudio()
{
static const char* dlls[] =
{
"xaudio2_7.dll",
"xaudio2_8.dll",
"xaudio2_9.dll"
};

for (const char* dllName : dlls)
{
    HMODULE module =
        GetModuleHandleA(
            dllName
        );


    if (module)
    {
        bool alreadyHooked = false;


        if (_stricmp(
                dllName,
                "xaudio2_7.dll"
            ) == 0)
        {
            alreadyHooked =
                g_xaudio27Hooked;
        }


        if (_stricmp(
                dllName,
                "xaudio2_8.dll"
            ) == 0)
        {
            alreadyHooked =
                g_xaudio28Hooked;
        }


        if (_stricmp(
                dllName,
                "xaudio2_9.dll"
            ) == 0)
        {
            alreadyHooked =
                g_xaudio29Hooked;
        }


        if (!alreadyHooked)
        {
            Log(
                ">>> XAudio DLL DETECTED: %s",
                dllName
            );


            InstallXAudioHook(
                dllName
            );
        }
    }
}

}

// ============================================================
// DLL LOAD HOOK TYPES
// ============================================================

typedef HMODULE (WINAPI* LoadLibraryA_t)(
LPCSTR
);

typedef HMODULE (WINAPI* LoadLibraryW_t)(
LPCWSTR
);

typedef HMODULE (WINAPI* LoadLibraryExA_t)(
LPCSTR,
HANDLE,
DWORD
);

typedef HMODULE (WINAPI* LoadLibraryExW_t)(
LPCWSTR,
HANDLE,
DWORD
);

// ============================================================
// ORIGINAL LOADLIBRARY POINTERS
// ============================================================

static LoadLibraryA_t
g_originalLoadLibraryA = nullptr;

static LoadLibraryW_t
g_originalLoadLibraryW = nullptr;

static LoadLibraryExA_t
g_originalLoadLibraryExA = nullptr;

static LoadLibraryExW_t
g_originalLoadLibraryExW = nullptr;

// ============================================================
// CHECK DLL NAME
// ============================================================

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
    s.find("xaudio2_7.dll") != std::string::npos ||
    s.find("xaudio2_8.dll") != std::string::npos ||
    s.find("xaudio2_9.dll") != std::string::npos ||
    s == "xaudio2.dll";

}

// ============================================================
// LOADLIBRARY A HOOK
// ============================================================

static HMODULE WINAPI
HookedLoadLibraryA(
LPCSTR lpLibFileName
)
{
if (lpLibFileName &&
IsXAudioName(lpLibFileName))
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


if (lpLibFileName &&
    IsXAudioName(lpLibFileName))
{
    Log(
        ">>> LoadLibraryA RESULT: %p",
        static_cast<void*>(module)
    );


    if (module)
    {
        // Give the loader a moment to finish
        // initializing the DLL.

        Sleep(10);

        ScanForXAudio();
    }
}


return module;

}

// ============================================================
// LOADLIBRARY W HOOK
// ============================================================

static HMODULE WINAPI
HookedLoadLibraryW(
LPCWSTR lpLibFileName
)
{
HMODULE module =
g_originalLoadLibraryW(
lpLibFileName
);

if (lpLibFileName)
{
    char name[512] = {};


    WideCharToMultiByte(
        CP_ACP,
        0,
        lpLibFileName,
        -1,
        name,
        sizeof(name),
        nullptr,
        nullptr
    );


    if (IsXAudioName(name))
    {
        Log(
            ">>> LoadLibraryW XAudio: %s",
            name
        );


        Log(
            ">>> Module = %p",
            static_cast<void*>(module)
        );


        if (module)
        {
            Sleep(10);

            ScanForXAudio();
        }
    }
}


return module;

}

// ============================================================
// LOADLIBRARYEX A HOOK
// ============================================================

static HMODULE WINAPI
HookedLoadLibraryExA(
LPCSTR lpLibFileName,
HANDLE hFile,
DWORD dwFlags
)
{
if (lpLibFileName &&
IsXAudioName(lpLibFileName))
{
Log(
">>> LoadLibraryExA REQUEST: %s",
lpLibFileName
);
}

HMODULE module =
    g_originalLoadLibraryExA(
        lpLibFileName,
        hFile,
        dwFlags
    );


if (lpLibFileName &&
    IsXAudioName(lpLibFileName))
{
    Log(
        ">>> LoadLibraryExA RESULT: %p",
        static_cast<void*>(module)
    );


    if (module)
    {
        Sleep(10);

        ScanForXAudio();
    }
}


return module;

}

// ============================================================
// LOADLIBRARYEX W HOOK
// ============================================================

static HMODULE WINAPI
HookedLoadLibraryExW(
LPCWSTR lpLibFileName,
HANDLE hFile,
DWORD dwFlags
)
{
HMODULE module =
g_originalLoadLibraryExW(
lpLibFileName,
hFile,
dwFlags
);

if (lpLibFileName)
{
    char name[512] = {};


    WideCharToMultiByte(
        CP_ACP,
        0,
        lpLibFileName,
        -1,
        name,
        sizeof(name),
        nullptr,
        nullptr
    );


    if (IsXAudioName(name))
    {
        Log(
            ">>> LoadLibraryExW XAudio: %s",
            name
        );


        Log(
            ">>> Module = %p",
            static_cast<void*>(module)
        );


        if (module)
        {
            Sleep(10);

            ScanForXAudio();
        }
    }
}


return module;

}

// ============================================================
// INSTALL LOADLIBRARY HOOKS
// ============================================================

static void InstallLoadLibraryHooks()
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


// --------------------------------------------------------
// LoadLibraryA
// --------------------------------------------------------

FARPROC loadA =
    GetProcAddress(
        kernel32,
        "LoadLibraryA"
    );


if (loadA)
{
    MH_STATUS status =
        MH_CreateHook(
            reinterpret_cast<LPVOID>(
                loadA
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
                    loadA
                )
            );


        Log(
            "LoadLibraryA hook enable = %d",
            static_cast<int>(status)
        );
    }
}


// --------------------------------------------------------
// LoadLibraryW
// --------------------------------------------------------

FARPROC loadW =
    GetProcAddress(
        kernel32,
        "LoadLibraryW"
    );


if (loadW)
{
    MH_STATUS status =
        MH_CreateHook(
            reinterpret_cast<LPVOID>(
                loadW
            ),
            reinterpret_cast<LPVOID>(
                &HookedLoadLibraryW
            ),
            reinterpret_cast<LPVOID*>(
                &g_originalLoadLibraryW
            )
        );


    Log(
        "LoadLibraryW hook create = %d",
        static_cast<int>(status)
    );


    if (status == MH_OK)
    {
        status =
            MH_EnableHook(
                reinterpret_cast<LPVOID>(
                    loadW
                )
            );


        Log(
            "LoadLibraryW hook enable = %d",
            static_cast<int>(status)
        );
    }
}


// --------------------------------------------------------
// LoadLibraryExA
// --------------------------------------------------------

FARPROC loadExA =
    GetProcAddress(
        kernel32,
        "LoadLibraryExA"
    );


if (loadExA)
{
    MH_STATUS status =
        MH_CreateHook(
            reinterpret_cast<LPVOID>(
                loadExA
            ),
            reinterpret_cast<LPVOID>(
                &HookedLoadLibraryExA
            ),
            reinterpret_cast<LPVOID*>(
                &g_originalLoadLibraryExA
            )
        );


    Log(
        "LoadLibraryExA hook create = %d",
        static_cast<int>(status)
    );


    if (status == MH_OK)
    {
        status =
            MH_EnableHook(
                reinterpret_cast<LPVOID>(
                    loadExA
                )
            );


        Log(
            "LoadLibraryExA hook enable = %d",
            static_cast<int>(status)
        );
    }
}


// --------------------------------------------------------
// LoadLibraryExW
// --------------------------------------------------------

FARPROC loadExW =
    GetProcAddress(
        kernel32,
        "LoadLibraryExW"
    );


if (loadExW)
{
    MH_STATUS status =
        MH_CreateHook(
            reinterpret_cast<LPVOID>(
                loadExW
            ),
            reinterpret_cast<LPVOID>(
                &HookedLoadLibraryExW
            ),
            reinterpret_cast<LPVOID*>(
                &g_originalLoadLibraryExW
            )
        );


    Log(
        "LoadLibraryExW hook create = %d",
        static_cast<int>(status)
    );


    if (status == MH_OK)
    {
        status =
            MH_EnableHook(
                reinterpret_cast<LPVOID>(
                    loadExW
                )
            );


        Log(
            "LoadLibraryExW hook enable = %d",
            static_cast<int>(status)
        );
    }
}


Log(
    "LoadLibrary hook initialization finished."
);

}

// ============================================================
// BACKGROUND XAudio SCANNER
// ============================================================

static DWORD WINAPI
XAudioScannerThread(
LPVOID
)
{
Log(
"XAudio background scanner started."
);

while (g_running)
{
    ScanForXAudio();

    Sleep(250);
}


Log(
    "XAudio background scanner stopped."
);


return 0;

}

// ============================================================
// INITIALIZATION THREAD
// ============================================================

static DWORD WINAPI
InitThread(
LPVOID
)
{
// --------------------------------------------------------
// Paths
// --------------------------------------------------------

InitializePaths();


// --------------------------------------------------------
// Fresh log
// --------------------------------------------------------

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
    "Game executable directory:"
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


// --------------------------------------------------------
// Config
// --------------------------------------------------------

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


if (status != MH_OK &&
    status != MH_ERROR_ALREADY_INITIALIZED)
{
    Log(
        "ERROR: MinHook initialization failed."
    );

    return 0;
}


// --------------------------------------------------------
// Install LoadLibrary monitoring
// --------------------------------------------------------

InstallLoadLibraryHooks();


// --------------------------------------------------------
// Scan immediately
// --------------------------------------------------------

Log(
    "Performing initial XAudio scan..."
);


ScanForXAudio();


// --------------------------------------------------------
// Start background scanner
// --------------------------------------------------------

HANDLE scanner =
    CreateThread(
        nullptr,
        0,
        XAudioScannerThread,
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
        "ERROR: Could not create XAudio scanner thread."
    );
}


// --------------------------------------------------------
// Finished
// --------------------------------------------------------

Log(
    "================================================"
);

Log(
    "Initialization complete."
);

Log(
    "Waiting for Skyrim to load XAudio..."
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
if (reason ==
DLL_PROCESS_ATTACH)
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
        CloseHandle(thread);
    }
}


else if (reason ==
         DLL_PROCESS_DETACH)
{
    g_running = false;

    MH_Uninitialize();
}


return TRUE;

}


