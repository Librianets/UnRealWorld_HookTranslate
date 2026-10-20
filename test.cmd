@echo off
setlocal
cd /d "%~dp0"
TranslationCatalogTest.exe
if errorlevel 1 exit /b 1
CompactCatalogTest.exe tests\fixture
if errorlevel 1 exit /b 1
URWTraceSelfTest.exe
if errorlevel 1 exit /b 1
URWTranslationSelfTest.exe
if errorlevel 1 exit /b 1
echo Native catalogue and synthetic hook tests passed. No game was launched.
