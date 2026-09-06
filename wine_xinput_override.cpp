#include <windows.h>
#include <string>
#include <vector>

typedef void (__cdecl *pfnWineDllSetConfig)(const char* load_order, const char* app_name);

const std::vector<std::string> XINPUT_MODULES = {
    "xinput1_4",
    "xinput1_3",
    "xinput1_2",
    "xinput1_1",
    "xinput9_1_0"
};

void ForceWineNativeXInput() {
    // 1. Always write directly to Wine Registry HKCU\Software\Wine\DllOverrides
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\DllOverrides", 0, NULL, 
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        for (const auto& mod : XINPUT_MODULES) {
            RegSetValueExA(hKey, mod.c_str(), 0, REG_SZ, (const BYTE*)"native,builtin", 15);
        }
        RegCloseKey(hKey);
    }

    // 2. Try in-memory set via ntdll export if available
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (hNtdll) {
        auto pwine_dll_set_config = (pfnWineDllSetConfig)GetProcAddress(hNtdll, "wine_dll_set_config");
        if (pwine_dll_set_config) {
            std::string overrideConfig = "";
            for (size_t i = 0; i < XINPUT_MODULES.size(); ++i) {
                overrideConfig += XINPUT_MODULES[i] + "=n,b";
                if (i < XINPUT_MODULES.size() - 1) {
                    overrideConfig += ";";
                }
            }
            pwine_dll_set_config(overrideConfig.c_str(), "");
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
