@echo off
setlocal
cd /d "%~dp0"
call "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1
if not exist .build mkdir .build
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /Fo.build\ /Fe:SteamEntryTest.exe src\SteamInstallTest.cpp /link /MACHINE:X86 advapi32.lib
if errorlevel 1 exit /b 1
SteamEntryTest.exe
