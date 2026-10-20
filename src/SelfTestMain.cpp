#include "Shared.h"
int wmain() {
    std::wstring path=Directory(ModulePath())+L"\\URWTraceHook.dll";
    HMODULE dll=LoadLibraryW(path.c_str());
    if(!dll){printf("Cannot load hook DLL: %lu\n",GetLastError());return 2;}
    auto test=(DWORD(WINAPI*)(void*))GetProcAddress(dll,"RunTraceSelfTest");
    DWORD result=test?test(nullptr):0;
    printf("Native x86 self-test: %s. See SELFTEST.json\n",result==1?"PASS":"FAIL");
    FreeLibrary(dll);return result==1?0:1;
}
