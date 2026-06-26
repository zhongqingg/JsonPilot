@echo off
setlocal
echo ==========================================
echo   JsonPilot - Build ^& Deploy Script
echo   Targets: JsonPilotBackend, JsonPilotViewer
echo ==========================================
echo.

:: Step 1: Clean old artifacts
echo [1/5] Cleaning old build artifacts...
if exist "build\src\resources.rc" del /q "build\src\resources.rc"
echo.

:: Step 2: Configure CMake
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

:: Step 3: Build both targets
echo.
echo [3/5] Building JsonPilotBackend ^(Release^)...
cmake --build build --config Release --target JsonPilotBackend
if errorlevel 1 (
    echo ERROR: Backend build failed!
    exit /b 1
)

echo.
echo [3b/5] Building JsonPilotViewer ^(Release^)...
cmake --build build --config Release --target JsonPilotViewer
if errorlevel 1 (
    echo ERROR: Viewer build failed!
    exit /b 1
)

:: Step 4: Create deploy folder
echo.
echo [4/5] Creating deploy folder...
if exist "deploy\JsonPilot" rmdir /s /q "deploy\JsonPilot"
mkdir "deploy\JsonPilot\web"
mkdir "deploy\JsonPilot\data"

:: Copy executables
for %%t in (JsonPilotBackend JsonPilotViewer) do (
    if exist "build\Release\%%t.exe" (
        copy /y "build\Release\%%t.exe" "deploy\JsonPilot\" >nul
        echo   Copied %%t.exe
    ) else (
        echo   WARNING: %%t.exe not found!
    )
)

:: Copy WebView2Loader.dll
if exist "build\Release\WebView2Loader.dll" (
    copy /y "build\Release\WebView2Loader.dll" "deploy\JsonPilot\" >nul
    echo   Copied WebView2Loader.dll
) else if exist "thirdparty\WebView2Loader.dll" (
    copy /y "thirdparty\WebView2Loader.dll" "deploy\JsonPilot\" >nul
    echo   Copied WebView2Loader.dll (from thirdparty)
)

:: Copy web folder
xcopy /y /e "web\*" "deploy\JsonPilot\web\" >nul
echo   Copied web files

:: Copy icons
if exist "src\icon.png" copy /y "src\icon.png" "deploy\JsonPilot\web\icon.png" >nul
if exist "src\icon.ico" copy /y "src\icon.ico" "deploy\JsonPilot\web\icon.ico" >nul
echo   Copied icon files

:: Copy sample data
if not exist "deploy\JsonPilot\data\*" (
    if exist "data\*" xcopy /y /e /i "data\*" "deploy\JsonPilot\data\" >nul
)

echo.
echo [5/5] Deploy complete!
echo.
echo ==========================================
echo   Output: deploy\JsonPilot\
echo   ^> JsonPilotBackend.exe  (headless server)
echo   ^> JsonPilotViewer.exe   (WebView2 GUI)
echo   ^> WebView2Loader.dll
echo   ^> web\ ^(index.html, script.js, style.css, etc.^)
echo   ^> data\
echo.
echo   To run:
echo     Backend: deploy\JsonPilot\JsonPilotBackend.exe --backend
echo     Viewer:  deploy\JsonPilot\JsonPilotViewer.exe [file.json]
echo.
echo   To create installer (requires NSIS):
echo     cd deploy ^&^& makensis installer.nsi
echo ==========================================
endlocal
