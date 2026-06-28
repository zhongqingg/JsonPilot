; JsonPilot NSIS Installer Script (dual-process)
; Requires NSIS 3.0+ (https://nsis.sourceforge.io/)
; Build: cd deploy && makensis installer.nsi

!define PRODUCT_NAME "JsonPilot"
!define PRODUCT_VERSION "1.4.0"
!define PRODUCT_PUBLISHER "Donchy"
!define PRODUCT_WEB_SITE "https://github.com/zhongqingg/JsonPilot"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\JsonPilotViewer.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

SetCompressor lzma

; MUI Settings
!include "MUI2.nsh"

!define MUI_ABORTWARNING
!define MUI_ICON "..\src\icon.ico"
!define MUI_UNICON "..\src\icon.ico"

; Welcome page
!insertmacro MUI_PAGE_WELCOME
; Directory page
!insertmacro MUI_PAGE_DIRECTORY
; Instfiles page
!insertmacro MUI_PAGE_INSTFILES
; Finish page
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "English"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "JsonPilot-${PRODUCT_VERSION}-Setup.exe"
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" ""
ShowInstDetails show
ShowUnInstDetails show

; Kill running backend process before install (avoids locked file)
!define INSTALL_PREPARE 'ExecWait "taskkill /f /im JsonPilotBackend.exe"'

Section "MainSection" SEC01
    ; Kill any running backend before overwriting
    ExecWait "taskkill /f /im JsonPilotBackend.exe"

    SetOutPath "$INSTDIR"
    SetOverwrite on

    ; Application files
    File "JsonPilot\JsonPilotBackend.exe"
    File "JsonPilot\JsonPilotViewer.exe"
    File "JsonPilot\WebView2Loader.dll"
    File "JsonPilot\config.json"

    ; Web files
    SetOutPath "$INSTDIR\web"
    File "JsonPilot\web\index.html"
    File "JsonPilot\web\script.js"
    File "JsonPilot\web\style.css"
    File "JsonPilot\web\icon.png"
    File "JsonPilot\web\icon.ico"
    File "JsonPilot\web\ping"

    ; Data directory (empty, for user JSON files)
    CreateDirectory "$INSTDIR\data"
SectionEnd

Section -AdditionalIcons
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    ; Only viewer shortcut on desktop
    CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\JsonPilotViewer.exe"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\JsonPilotViewer.exe"
    CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" "$INSTDIR\uninst.exe"
SectionEnd

Section -Post
    WriteUninstaller "$INSTDIR\uninst.exe"

    ; Register backend for auto-start on boot (current user only)
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Run" \
        "JsonPilotBackend" '"$INSTDIR\JsonPilotBackend.exe" --backend'

    ; Register file association for .json
    WriteRegStr HKCU "Software\Classes\Applications\JsonPilotViewer.exe\shell\open\command" \
        "" '"$INSTDIR\JsonPilotViewer.exe" "%1"'
    WriteRegStr HKCU "Software\Classes\Applications\JsonPilotViewer.exe\SupportedTypes" \
        ".json" ""
    WriteRegStr HKCU "Software\Classes\JsonPilot.json\DefaultIcon" \
        "" "$INSTDIR\JsonPilotViewer.exe,0"
    WriteRegStr HKCU "Software\Classes\JsonPilot.json\shell\open\command" \
        "" '"$INSTDIR\JsonPilotViewer.exe" "%1"'
    WriteRegStr HKCU "Software\Classes\.json\OpenWithProgids" \
        "JsonPilot.json" ""

    WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\JsonPilotViewer.exe"
    WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "Path" "$INSTDIR"
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\uninst.exe"
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\JsonPilotViewer.exe"
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
    WriteRegStr HKLM "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
SectionEnd

Section Uninstall
    ; Kill backend process before deleting files
    ExecWait "taskkill /f /im JsonPilotBackend.exe"

    ; Remove auto-start registry entry
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "JsonPilotBackend"

    ; Remove file association
    DeleteRegKey HKCU "Software\Classes\Applications\JsonPilotViewer.exe"
    DeleteRegKey HKCU "Software\Classes\JsonPilot.json"
    DeleteRegValue HKCU "Software\Classes\.json\OpenWithProgids" "JsonPilot.json"

    Delete "$INSTDIR\uninst.exe"
    Delete "$INSTDIR\JsonPilotBackend.exe"
    Delete "$INSTDIR\JsonPilotViewer.exe"
    Delete "$INSTDIR\WebView2Loader.dll"
    Delete "$INSTDIR\config.json"

    RMDir /r "$INSTDIR\web"
    RMDir "$INSTDIR\data"

    Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"
    RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

    RMDir "$INSTDIR"

    DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"
    DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"

    SetAutoClose true
SectionEnd
