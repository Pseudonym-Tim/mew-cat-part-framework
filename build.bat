@echo off
REM Build MewCatPartFramework.dll

set "DESTINATION_DIR=C:\Users\Pseudonym_Tim\Desktop\Tools\Mewtator\mods\MewCatPartFramework"
set "MEWTATOR_DEPLOY=true"

setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Is Visual Studio installed?
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"

if not defined VSDIR (
    echo ERROR: Could not find Visual Studio C++ Build Tools.
    pause
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

if errorlevel 1 (
    echo ERROR: Could not initialize the x64 MSVC environment.
    pause
    exit /b 1
)

set "CL="
set "_CL_="
set "CL_EXE=%VCToolsInstallDir%bin\Hostx64\x64\cl.exe"
set "LINK_EXE=%VCToolsInstallDir%bin\Hostx64\x64\link.exe"

if not exist "%CL_EXE%" (
    echo ERROR: cl.exe not found at "%CL_EXE%"
    pause
    exit /b 1
)

if not exist "%LINK_EXE%" (
    echo ERROR: link.exe not found at "%LINK_EXE%"
    pause
    exit /b 1
)

if not exist "src\MewCatPartFramework.c" (
    echo ERROR: Put this file in the project root beside the src folder.
    pause
    exit /b 1
)

if not exist "MewCatPartFramework.def" (
    echo ERROR: MewCatPartFramework.def is missing.
    pause
    exit /b 1
)

if not exist build mkdir build

REM Never let an old DLL make a failed build look successful.
if exist "MewCatPartFramework.dll" del /q "MewCatPartFramework.dll"
if exist "MewCatPartFramework.exp" del /q "MewCatPartFramework.exp"
if exist "MewCatPartFramework.lib" del /q "MewCatPartFramework.lib"

echo Building MewCatPartFramework.dll...

"%CL_EXE%" /nologo /c /O2 /W3 /MD /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Fo"build\MewCatPartFramework.obj" /Tc"%~dp0src\MewCatPartFramework.c"
if errorlevel 1 goto :failed

"%LINK_EXE%" /nologo /DLL /MACHINE:X64 /INCREMENTAL:NO /OUT:"MewCatPartFramework.dll" /IMPLIB:"build\MewCatPartFramework.lib" /DEF:"MewCatPartFramework.def" "build\MewCatPartFramework.obj" kernel32.lib
if errorlevel 1 goto :failed

if not exist "MewCatPartFramework.dll" (
    echo ERROR: Linker returned success, but MewCatPartFramework.dll was not created.
    goto :failed
)

echo.
echo Build succeeded: "%CD%\MewCatPartFramework.dll"

if /I "%MEWTATOR_DEPLOY%"=="true" (
    set "DEPLOY_DIR=%DESTINATION_DIR%"
) else (
    set "DEPLOY_DIR=%DESTINATION_DIR%\mods\MewCatPartFramework"
)

if not exist "%DEPLOY_DIR%" mkdir "%DEPLOY_DIR%"
if errorlevel 1 goto :deploy_failed

copy /Y "MewCatPartFramework.dll" "%DEPLOY_DIR%\MewCatPartFramework.dll" >nul
if errorlevel 1 goto :deploy_failed

if exist "description.json" (
    copy /Y "description.json" "%DEPLOY_DIR%\description.json" >nul
    if errorlevel 1 goto :deploy_failed
)

if exist "preview.png" (
    copy /Y "preview.png" "%DEPLOY_DIR%\preview.png" >nul
    if errorlevel 1 goto :deploy_failed
)

echo Deployed to "%DEPLOY_DIR%"
pause
exit /b 0

:deploy_failed
echo.
echo Build succeeded, but deployment FAILED.
echo DLL remains at "%CD%\MewCatPartFramework.dll"
pause
exit /b 1

:failed
echo.
echo Build FAILED.
pause
exit /b 1
