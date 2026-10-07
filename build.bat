@echo off
setlocal

set "VCVARS=C:\software\vs2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
set "BUILD_DIR=build"

if not exist "%VCVARS%" (
    echo Cannot find vcvarsall.bat: %VCVARS%
    exit /b 1
)

call "%VCVARS%" x64
if errorlevel 1 exit /b 1

where cmake >nul 2>nul
if errorlevel 1 (
    echo Cannot find cmake on PATH.
    exit /b 1
)

cd /d "%~dp0"

cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%"
if errorlevel 1 exit /b 1

:: Auto-deploy after a successful build.
:: Usage: build.bat [version]  (omit version to reuse the last one)
call "%~dp0deploy\deploy.bat" %1
if errorlevel 1 (
    echo.
    echo ERROR: deploy step failed!
    exit /b 1
)

endlocal