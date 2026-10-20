#include "Shared.h"

int wmain() {
    std::wstring path = Directory(ModulePath()) + L"\\URWTraceHook.dll";
    HMODULE dll = LoadLibraryW(path.c_str());
    if (!dll) {
        printf("Cannot load hook DLL: Win32 %lu\n", GetLastError());
        return 2;
    }
    auto test = reinterpret_cast<DWORD(WINAPI*)(void*)>(GetProcAddress(dll, "RunTranslationHookSelfTest"));
    if (!test) printf("Missing RunTranslationHookSelfTest: Win32 %lu\n", GetLastError());
    DWORD result = test ? test(nullptr) : 0;
    printf("Native translation hook integration: %s. See TRANSLATION_SELFTEST.json\n", result == 1 ? "PASS" : "FAIL");
    FreeLibrary(dll);
    return result == 1 ? 0 : 1;
}
