@echo off
setlocal
echo ==========================================
echo   JsonPilot - Build ^& Deploy Script
echo ==========================================
echo.

:: Step 1: Clean and reconfigure CMake
echo [1/5] Cleaning old build artifacts...
if exist "build\src\resources.rc" del /q "build\src\resources.rc"
echo.

echo [2/5] Configuring CMake...
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo ERROR: CMake configuration failed!
    echo Trying alternative generators...
    cmake -B build -S .
    if errorlevel 1 (
        echo ERROR: CMake configuration failed with all generators!
        exit /b 1
    )
)

:: Step 3: Build
echo.
echo [3/5] Building JsonPilot ^(Release^)...
cmake --build build --config Release
if errorlevel 1 (
    echo ERROR: Build failed!
    exit /b 1
)

:: Step 4: Create deploy folder
echo.
echo [4/5] Creating deploy folder...
if exist "deploy\JsonPilot" rmdir /s /q "deploy\JsonPilot"
mkdir "deploy\JsonPilot"
mkdir "deploy\JsonPilot\web"
mkdir "deploy\JsonPilot\data"

:: Copy executable
if exist "build\Release\JsonPilot.exe" (
    copy /y "build\Release\JsonPilot.exe" "deploy\JsonPilot\" >nul
    echo   Copied JsonPilot.exe
) else if exist "build\Release\jsoneditor.exe" (
    copy /y "build\Release\jsoneditor.exe" "deploy\JsonPilot\JsonPilot.exe" >nul
    echo   Copied jsoneditor.exe as JsonPilot.exe
) else if exist "build\JsonPilot.exe" (
    copy /y "build\JsonPilot.exe" "deploy\JsonPilot\" >nul
    echo   Copied JsonPilot.exe
) else (
    echo   WARNING: Could not find executable! Check build output.
)

:: Copy web folder
xcopy /y /e "web\*" "deploy\JsonPilot\web\" >nul
echo   Copied web files

:: Copy icons
if exist "src\icon.png" copy /y "src\icon.png" "deploy\JsonPilot\web\icon.png" >nul
if exist "src\icon.ico" copy /y "src\icon.ico" "deploy\JsonPilot\web\icon.ico" >nul
echo   Copied icon files

:: Copy data files
if exist "data\*" xcopy /y /e /i "data\*" "deploy\JsonPilot\data\" >nul

:: Create config
echo root_dir=data> "deploy\JsonPilot\config.txt"
echo theme=dark>> "deploy\JsonPilot\config.txt"

echo.
echo [5/5] Deploy complete!
echo.
echo ==========================================
echo   Output: deploy\JsonPilot\
echo   ^> JsonPilot.exe
echo   ^> config.txt
echo   ^> web\ ^(index.html, script.js, style.css, icon.png, icon.ico^)
echo   ^> data\
echo.
echo   To run: deploy\JsonPilot\JsonPilot.exe
echo.
echo   To create installer (requires NSIS):
echo     cd deploy ^&^& makensis installer.nsi
echo ==========================================
endlocal
