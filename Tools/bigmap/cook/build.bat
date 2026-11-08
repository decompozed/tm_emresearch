@echo off
rem Builds cook.exe and nxload.exe (x86: the game's PhysX 2.8.4 DLLs are 32-bit).
setlocal
set VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars32.bat
if not exist "%VCVARS%" set VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat
call "%VCVARS%" >nul || exit /b 1
cd /d %~dp0
if not exist obj mkdir obj
cl /nologo /O2 /W4 /EHsc /std:c++17 cook.cpp /Foobj\ /Fe:cook.exe || exit /b 1
cl /nologo /O2 /W4 /EHa /std:c++17 nxload.cpp /Foobj\ /Fe:nxload.exe || exit /b 1
