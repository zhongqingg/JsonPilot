@echo off
setlocal enabledelayedexpansion

:: deploy.bat [version]
:: ------------------------------------------------------------------
::  Package freshly built backend/viewer/web files and build the NSIS
::  installer.
::    * Always uses the latest build outputs from build\ (Ninja) or
::      build\Release\ (Visual Studio).
::    * If [version] is omitted, the last version stored in
::      deploy\version.txt is reused, so the previous installer of the
::      same version is overwritten (no version bump required).
:: ------------------------------------------------------------------

set "ROOT=%~dp0.."
pushd "%ROOT%" || (echo [deploy] ERROR: cannot enter %ROOT% & exit /b 1)

set "BUILD_DIR=build"
set "PKG_DIR=deploy\JsonPilot"
set "VERSION_FILE=deploy\version.txt"

:: --- 1. Resolve version -------------------------------------------
set "VERSION=%~1"
if not defined VERSION if exist "%VERSION_FILE%" set /p VERSION=<"%VERSION_FILE%"
if not defined VERSION set "VERSION=1.0.0"
for /f "tokens=1 delims= " %%v in ("!VERSION!") do set "VERSION=%%v"
echo [deploy] Version: !VERSION!

:: --- 2. Locate the latest build outputs ---------------------------
set "OUT_DIR="
if exist "%BUILD_DIR%\JsonPilotBackend.exe" set "OUT_DIR=%BUILD_DIR%"
if not defined OUT_DIR if exist "%BUILD_DIR%\Release\JsonPilotBackend.exe" set "OUT_DIR=%BUILD_DIR%\Release"
if not defined OUT_DIR (
    echo [deploy] ERROR: build output not found - compile first.
    popd & exit /b 1
)
echo [deploy] Using build output: %OUT_DIR%

for %%f in (JsonPilotBackend.exe JsonPilotViewer.exe WebView2Loader.dll) do (
    if not exist "%OUT_DIR%\%%f" (
        echo [deploy] ERROR: missing "%OUT_DIR%\%%f"
        popd & exit /b 1
    )
)

:: --- 3. Stage the package -----------------------------------------
:: If a JsonPilot process is running from this folder, Windows denies the
:: directory removal; ignore that (files are overwritten below anyway)
:: and detect real copy failures instead.
if exist "%PKG_DIR%" rmdir /s /q "%PKG_DIR%" 2>nul
mkdir "%PKG_DIR%" 2>nul
mkdir "%PKG_DIR%\web" 2>nul
mkdir "%PKG_DIR%\data" 2>nul

for %%f in (JsonPilotBackend.exe JsonPilotViewer.exe WebView2Loader.dll) do (
    copy /y "%OUT_DIR%\%%f" "%PKG_DIR%\" >nul
    if errorlevel 1 (
        echo [deploy] ERROR: cannot overwrite "%PKG_DIR%\%%f" ^(file in use?^).
        echo [deploy]        Close JsonPilotBackend/JsonPilotViewer and retry.
        popd & exit /b 1
    )
)

:: Prefer the web files copied next to the freshly built exe.
set "WEB_SRC=%OUT_DIR%\web"
if not exist "%WEB_SRC%\index.html" set "WEB_SRC=web"
echo [deploy] Web source: %WEB_SRC%
xcopy /y /e /i "%WEB_SRC%\*" "%PKG_DIR%\web\" >nul
if exist "src\icon.png" copy /y "src\icon.png" "%PKG_DIR%\web\icon.png" >nul
if exist "src\icon.ico" copy /y "src\icon.ico" "%PKG_DIR%\web\icon.ico" >nul

if not exist "%PKG_DIR%\config.json" >"%PKG_DIR%\config.json" echo {"theme":"dark","sessions":[]}

:: --- 4. Build the installer (overwrites same-version setup) -------
set "MAKENSIS="
for /f "delims=" %%p in ('where makensis 2^>nul') do if not defined MAKENSIS set "MAKENSIS=%%p"
if not defined MAKENSIS if exist "C:\software\NSIS\makensis.exe" set "MAKENSIS=C:\software\NSIS\makensis.exe"
if not defined MAKENSIS if exist "C:\Program Files (x86)\NSIS\makensis.exe" set "MAKENSIS=C:\Program Files (x86)\NSIS\makensis.exe"

if not defined MAKENSIS (
    echo [deploy] WARNING: makensis not found; package staged at %PKG_DIR% but installer skipped.
    popd & exit /b 0
)

pushd deploy
"%MAKENSIS%" /DPRODUCT_VERSION=!VERSION! installer.nsi
set "MKERR=!errorlevel!"
popd
if not "!MKERR!"=="0" (
    echo [deploy] ERROR: makensis failed ^(code !MKERR!^).
    popd & exit /b 1
)

:: --- 5. Persist the version for next time -------------------------
>"%VERSION_FILE%" echo !VERSION!

echo [deploy] OK - installer: deploy\JsonPilot-!VERSION!-Setup.exe
popd
endlocal
exit /b 0
