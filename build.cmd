@echo off
setlocal
cd /d "%~dp0"
if not defined VSCMD_ARG_TGT_ARCH (
  if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat" (
    call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat"
  ) else (
    echo Run this script from an x86 Visual Studio Native Tools Command Prompt.
    exit /b 1
  )
)
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x86" (
  echo This hook requires an x86 build environment.
  exit /b 1
)
if not exist .build mkdir .build
cl /nologo /c /O2 /MT /W3 /Ithird_party\minhook\include /Fo.build\ third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde32.c
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /utf-8 /O2 /Ob0 /Oy- /MT /EHsc /W4 /Fo.build\ src\TraceRuntime.cpp src\TraceSelfTest.cpp src\TranslationCatalog.cpp src\EncyclopediaFile.cpp src\TranslationHookSelfTest.cpp
if errorlevel 1 exit /b 1
link /nologo /DLL /MACHINE:X86 /OUT:URWTraceHook.dll /DEF:src\URWTraceHook.def .build\TraceRuntime.obj .build\TraceSelfTest.obj .build\TranslationCatalog.obj .build\EncyclopediaFile.obj .build\TranslationHookSelfTest.obj .build\buffer.obj .build\hook.obj .build\trampoline.obj .build\hde32.obj kernel32.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /Fo.build\ /Fe:URWTrace.exe src\Launcher.cpp /link /MACHINE:X86 /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup kernel32.lib user32.lib advapi32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /Fo.build\ /Fe:URWTraceSelfTest.exe src\SelfTestMain.cpp /link /MACHINE:X86 kernel32.lib advapi32.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /Fo.build\ /Fe:URWTranslationSelfTest.exe src\TranslationHookTestMain.cpp /link /MACHINE:X86 kernel32.lib advapi32.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /Fo.build\ /Fe:URWEncyclopediaSelfTest.exe src\EncyclopediaFile.cpp src\TranslationCatalog.cpp src\EncyclopediaFileTest.cpp /link /MACHINE:X86 kernel32.lib advapi32.lib user32.lib
if errorlevel 1 exit /b 1
if not exist .build\probe mkdir .build\probe
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /Fo.build\probe\ /Fe:CatalogProbe.exe src\CatalogProbe.cpp src\TranslationCatalog.cpp
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /Fo.build\probe\ /Fe:CatalogDocuments.exe src\CatalogDocuments.cpp src\TranslationCatalog.cpp src\EncyclopediaFile.cpp
if errorlevel 1 exit /b 1
if not exist .build\catalog_tests mkdir .build\catalog_tests
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /Fo.build\catalog_tests\ /Fe:TranslationCatalogTest.exe src\TranslationCatalogTest.cpp src\TranslationCatalog.cpp
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /Fo.build\catalog_tests\ /Fe:CompactCatalogTest.exe src\CompactCatalogTest.cpp src\TranslationCatalog.cpp
if errorlevel 1 exit /b 1
echo Built translation hook 0.22 and self-tests. GUI is in UnRealWorld_Launcher.
