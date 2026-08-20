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

if not exist build mkdir build

echo Building MewCatPartFramework.dll...

"%CL_EXE%" /nologo /c /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DNOMINMAX /Fo"build\MewCatPartFramework.obj" /Tc"%~dp0src\MewCatPartFramework.c"
if errorlevel 1 goto :failed

echo.
echo Build succeeded.

if /I "%MEWTATOR_DEPLOY%"=="true" (
    set "DEPLOY_DIR=%DESTINATION_DIR%"
) else (
    set "DEPLOY_DIR=%DESTINATION_DIR%\mods\MewCatPartFramework"
)

if not exist "%DEPLOY_DIR%" mkdir "%DEPLOY_DIR%"

copy /Y "MewCatPartFramework.dll" "%DEPLOY_DIR%\MewCatPartFramework.dll" >nul
if exist "description.json" copy /Y "description.json" "%DEPLOY_DIR%\description.json" >nul
if exist "preview.png" copy /Y "preview.png" "%DEPLOY_DIR%\preview.png" >nul

echo Deployed to "%DEPLOY_DIR%"
pause
exit /b 0

:failed
echo.
echo Build FAILED.
pause
exit /b 1