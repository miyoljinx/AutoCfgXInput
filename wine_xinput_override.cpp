#include <windows.h>
#include <string>
#include <vector>

// Pointer to Wine's internal DLL configuration exporter inside ntdll.dll
typedef void (__cdecl *pfnWineDllSetConfig)(const char* load_order, const char* app_name);

const std::vector<std::string> XINPUT_MODULES = {
    "xinput1_4",
    "xinput1_3",
    "xinput1_2",
    "xinput1_1",
    "xinput9_1_0"
};

void ForceWineNativeXInput() {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) return;

    // Check if running inside Wine / Proton environment
    auto pwine_dll_set_config = (pfnWineDllSetConfig)GetProcAddress(hNtdll, "wine_dll_set_config");

    if (pwine_dll_set_config) {
        // String format expected by Wine: "xinput1_4=n,b;xinput1_3=n,b;..."
        // 'n' = Native (Durazno in game folder)
        // 'b' = Builtin (Wine fallback)
        std::string overrideConfig = "";
        for (size_t i = 0; i < XINPUT_MODULES.size(); ++i) {
            overrideConfig += XINPUT_MODULES[i] + "=n,b";
            if (i < XINPUT_MODULES.size() - 1) {
                overrideConfig += ";";
            }
        }

        // 1. Force the override directly into Wine's active memory for current process
        pwine_dll_set_config(overrideConfig.c_str(), "");

        // 2. Persist to HKCU\Software\Wine\DllOverrides registry in Wine prefix
        HKEY hKey;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\DllOverrides", 0, NULL, 
                            REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            for (const auto& mod : XINPUT_MODULES) {
                RegSetValueExA(hKey, mod.c_str(), 0, REG_SZ, (const BYTE*)"native,builtin", 15);
            }
            RegCloseKey(hKey);
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        ForceWineNativeXInput();
    }
    return TRUE;
}
