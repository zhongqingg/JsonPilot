#include "webui.hpp"
#include "nlohmann/json.hpp"
#include "src/shared.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <windows.h>
#include <shobjidl.h>
#include <comdef.h>
#include <oleidl.h>
#include <ctime>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

static void logDebug(const char* msg) {
    std::time_t t = std::time(nullptr);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    std::string line = std::string("[") + buf + "] " + msg + "\n";
    std::cout << line;
    OutputDebugStringA(line.c_str());
    std::ofstream log("JsonPilot_debug.log", std::ios::app);
    if (log.is_open()) { log << line; log.close(); }
}

#define WM_SETUP_DRAGDROP (WM_APP + 1)

static std::string* g_subclassDropPath = NULL;
static webui::window* g_subclassWin = NULL;

// --- COM IDropTarget ---
class FileDropTarget : public IDropTarget {
private:
    LONG m_refCount = 1;
    std::string* m_outPath;
    webui::window* m_win;
public:
    FileDropTarget(std::string* outPath, webui::window* win) : m_outPath(outPath), m_win(win) {}
    virtual ~FileDropTarget() {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) { *ppv = this; AddRef(); return S_OK; }
        *ppv = NULL; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&m_refCount);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj, DWORD, POINTL, DWORD* pdwEffect) override {
        FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        *pdwEffect = SUCCEEDED(pDataObj->QueryGetData(&fmt)) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        logDebug(*pdwEffect == DROPEFFECT_COPY ? "[OLE_Enter] Accepting" : "[OLE_Enter] Rejecting");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* pdwEffect) override { *pdwEffect = DROPEFFECT_COPY; return S_OK; }
    HRESULT STDMETHODCALLTYPE DragLeave() override { logDebug("[OLE_Leave] DragLeave called"); return S_OK; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj, DWORD, POINTL, DWORD* pdwEffect) override {
        *pdwEffect = DROPEFFECT_NONE;
        FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM med = {};
        logDebug("[OLE_Drop] Drop() called");
        if (FAILED(pDataObj->GetData(&fmt, &med))) { logDebug("[OLE_Drop] GetData failed"); return S_OK; }
        HDROP hDrop = (HDROP)GlobalLock(med.hGlobal);
        if (!hDrop) { ReleaseStgMedium(&med); logDebug("[OLE_Drop] GlobalLock failed"); return S_OK; }
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
        logDebug(("[OLE_Drop] fileCount=" + std::to_string(count)).c_str());
        for (UINT i = 0; i < count; i++) {
            wchar_t path[MAX_PATH] = {};
            DragQueryFileW(hDrop, i, path, MAX_PATH);
            std::wstring ws(path);
            int utf8len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, NULL, 0, NULL, NULL);
            std::string utf8path;
            if (utf8len > 0) { utf8path.resize(utf8len - 1); WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &utf8path[0], utf8len, NULL, NULL); }
            logDebug(("[OLE_Drop] file=" + utf8path).c_str());
            size_t len = ws.size();
            if (len >= 5 && _wcsicmp(ws.c_str() + len - 5, L".json") == 0) {
                int l = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
                if (l > 0) {
                    m_outPath->resize(l - 1);
                    WideCharToMultiByte(CP_UTF8, 0, path, -1, &(*m_outPath)[0], l, NULL, NULL);
                    logDebug(("[OLE_Drop] SAVED path=" + *m_outPath).c_str());
                    // Read file content in C++ and push directly to frontend
                    // (HTML5 drop event won't fire since we replaced WebView2's OLE handler)
                    try {
                        std::ifstream f(fs::u8path(*m_outPath), std::ios::binary);
                        if (f.is_open()) {
                            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                            f.close();
                            // Escape for JSON (safe for JS eval)
                            auto jsonEscape = [](const std::string& s) {
                                std::string r; r.reserve(s.size() + 8); r += '"';
                                for (char c : s) {
                                    if (c == '"') r += "\\\"";
                                    else if (c == '\\') r += "\\\\";
                                    else if (c == '\n') r += "\\n";
                                    else if (c == '\r') r += "\\r";
                                    else if (c == '\t') r += "\\t";
                                    else r += c;
                                }
                                r += '"';
                                return r;
                            };
                            std::string js = "openDroppedFile(" + jsonEscape(*m_outPath) + "," + jsonEscape(content) + ")";
                            logDebug("[OLE_Drop] Pushing to frontend via win.run()");
                            m_win->run(js);
                        } else {
                            logDebug("[OLE_Drop] Cannot open dropped file for reading");
                        }
                    } catch (const std::exception& e) {
                        logDebug(("[OLE_Drop] Exception reading file: " + std::string(e.what())).c_str());
                    }
                }
                break;
            }
        }
        GlobalUnlock(med.hGlobal);
        ReleaseStgMedium(&med);
        *pdwEffect = DROPEFFECT_COPY;
        return S_OK;
    }
};

// Subclass proc for the main window — runs on webui thread (where window lives)
// Handles WM_SETUP_DRAGDROP to set up OLE + DragAcceptFiles on correct thread
static LRESULT CALLBACK MainWndSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_SETUP_DRAGDROP) {
        logDebug("[WNDPROC] WM_SETUP_DRAGDROP received — setting up drag-drop on webui thread");
        HRESULT hrOle = OleInitialize(NULL);
        logDebug(("[WNDPROC] OleInitialize hr=0x" + std::to_string(static_cast<long>(hrOle))).c_str());
        if (g_subclassDropPath) {
            FileDropTarget* target = new FileDropTarget(g_subclassDropPath, g_subclassWin);
            HRESULT hr = RegisterDragDrop(hwnd, target);
            logDebug(("[WNDPROC] RegisterDragDrop(main) hr=0x" + std::to_string(static_cast<long>(hr))).c_str());
        }
        for (int r = 0; r < 100; r++) {
            HWND childWV1 = NULL, childRHW = NULL;
            EnumChildWindows(hwnd, [](HWND h, LPARAM lp) -> BOOL {
                auto* results = reinterpret_cast<HWND*>(lp);
                wchar_t cls[128] = {}; GetClassNameW(h, cls, 128);
                if (wcscmp(cls, L"Chrome_WidgetWin_1") == 0) results[0] = h;
                if (wcscmp(cls, L"Chrome_RenderWidgetHostHWND") == 0) results[1] = h;
                return TRUE;
            }, reinterpret_cast<LPARAM>(&childWV1));
            if (childWV1 || childRHW) {
                if (childWV1) {
                    RevokeDragDrop(childWV1);
                    FileDropTarget* t2 = new FileDropTarget(g_subclassDropPath, g_subclassWin);
                    RegisterDragDrop(childWV1, t2);
                    logDebug("[WNDPROC] Registered on Chrome_WidgetWin_1");
                }
                if (childRHW) {
                    RevokeDragDrop(childRHW);
                    FileDropTarget* t3 = new FileDropTarget(g_subclassDropPath, g_subclassWin);
                    RegisterDragDrop(childRHW, t3);
                    logDebug("[WNDPROC] Registered on Chrome_RenderWidgetHostHWND");
                }
                break;
            }
            Sleep(30);
        }
        logDebug("[WNDPROC] Drag-drop setup complete");
        return 0;
    }
    WNDPROC oldMain = (WNDPROC)GetPropW(hwnd, L"OLD_MAIN_PROC");
    if (oldMain) return CallWindowProcW(oldMain, hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

class JsonEditorApp {
public:
    JsonEditorApp(const std::string& singleFilePath = "") {
        m_singleFilePath = singleFilePath;
        if (!m_singleFilePath.empty()) {
            fs::path p = fs::u8path(m_singleFilePath);
            if (!p.is_absolute()) {
                p = fs::absolute(p);
                m_singleFilePath = p.u8string();
            }
            if (fs::exists(p)) {
                m_singleFileMode = true;
                rootDir = p.parent_path().u8string();
                logDebug(("[MODE] Single-file mode: " + m_singleFilePath).c_str());
                logDebug(("[MODE] rootDir set to: " + rootDir).c_str());
            } else {
                logDebug(("[MODE] File not found, falling back to normal mode: " + m_singleFilePath).c_str());
                m_singleFilePath.clear();
            }
        }

        if (!m_singleFileMode) {
            loadConfig();
            scanJsonFiles();
        }

        win.bind("get_file_tree", this, &JsonEditorApp::bindGetFileTree);
        win.bind("load_file", this, &JsonEditorApp::bindLoadFile);
        win.bind("save_file", this, &JsonEditorApp::bindSaveFile);
        win.bind("save_file_as", this, &JsonEditorApp::bindSaveFileAs);
        win.bind("save_raw_file", this, &JsonEditorApp::bindSaveRawFile);
        win.bind("get_config", this, &JsonEditorApp::bindGetConfig);
        win.bind("show_save_dialog", this, &JsonEditorApp::bindShowSaveDialog);
        win.bind("browse_folder", this, &JsonEditorApp::bindBrowseFolder);
        win.bind("set_root_dir", this, &JsonEditorApp::bindSetRootDir);
        win.bind("create_folder", this, &JsonEditorApp::bindCreateFolder);
        win.bind("delete_path", this, &JsonEditorApp::bindDeletePath);
        win.bind("rename_path", this, &JsonEditorApp::bindRenamePath);
        win.bind("copy_file", this, &JsonEditorApp::bindCopyFile);
        win.bind("create_file", this, &JsonEditorApp::bindCreateFile);
        win.bind("get_last_drop_path", this, &JsonEditorApp::bindGetDropPath);
        win.bind("get_app_mode", this, &JsonEditorApp::bindGetAppMode);
        win.bind("log", this, &JsonEditorApp::bindLog);
        win.set_close_handler_wv(closeHandler);
    }

    void run() {
        std::string webPath = resolveWebPath();
        logDebug(("[INIT] Serving web folder: " + webPath).c_str());
        logDebug(("[INIT] JSON root dir: " + rootDir).c_str());
        if (m_singleFileMode) {
            logDebug(("[INIT] Mode: single-file, target: " + m_singleFilePath).c_str());
        } else {
            logDebug("[INIT] Mode: directory-browser");
        }

        // Inject mode & file content into HTML before page loads — avoids WebSocket binding delay
        bool htmlPatched = false;
        std::string htmlBackup;
        if (m_singleFileMode) {
            std::ifstream fi(fs::u8path(m_singleFilePath), std::ios::binary);
            std::string fileContent;
            bool fileReadOk = fi.is_open();
            if (fileReadOk) {
                fileContent.assign((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
                fi.close();
            }

            std::string indexPath = webPath + "\\index.html";
            std::ifstream fhtml(indexPath);
            if (fhtml.is_open()) {
                std::stringstream ss;
                ss << fhtml.rdbuf();
                htmlBackup = ss.str();
                fhtml.close();

                json mj;
                mj["singleFile"] = true;
                mj["filePath"] = fs::absolute(fs::u8path(m_singleFilePath)).u8string();
                std::string modeJson = mj.dump();

                // Escape file content for JS single-quoted string
                std::string escaped;
                if (fileReadOk) {
                    escaped.reserve(fileContent.size() + 16);
                    for (char c : fileContent) {
                        if (c == '\\') escaped += "\\\\";
                        else if (c == '\'') escaped += "\\'";
                        else if (c == '\n') escaped += "\\n";
                        else if (c == '\r') escaped += "\\r";
                        else if (c == '\t') escaped += "\\t";
                        else escaped += c;
                    }
                }

                std::string modeScript = "<script>window.__pilotMode=" + modeJson
                    + (fileReadOk ? ";window.__pilotFileContent='" + escaped + "'" : "")
                    + ";</script>";

                size_t pos = htmlBackup.find("<head>");
                if (pos != std::string::npos) {
                    std::string modified = htmlBackup;
                    modified.insert(pos + 6, "\n" + modeScript);
                    std::ofstream fo(indexPath);
                    if (fo.is_open()) {
                        fo << modified;
                        fo.close();
                        htmlPatched = true;
                        logDebug((std::string("[INIT] Injected __pilotMode") + (fileReadOk ? "+fileContent" : "") + " into index.html").c_str());
                    }
                }
            }
        }

        win.set_root_folder(webPath);
        if (!win.show_wv("")) {
            win.show("");
        }

        // Restore original HTML immediately — page is already loaded
        if (htmlPatched) {
            std::string indexPath = webPath + "\\index.html";
            std::ofstream fo(indexPath);
            if (fo.is_open()) {
                fo << htmlBackup;
                fo.close();
                logDebug("[INIT] Restored original index.html");
            }
        }

        // --- Drag-drop capture setup ---
        // CRITICAL: webui creates the window on a separate thread (webui thread).
        // OLE RegisterDragDrop and OleInitialize MUST run on that same thread.
        // We use SendMessage + subclass to execute setup on the correct thread.
        logDebug("[INIT] === Setting up drag-drop capture (on webui thread) ===");

        // Wait for main HWND
        for (int r = 0; r < 200 && !m_hWnd; r++) {
            m_hWnd = (HWND)win.win32_get_hwnd();
            if (!m_hWnd) Sleep(30);
        }
        if (!m_hWnd) {
            logDebug("[INIT] FAILED to get HWND");
        } else {
            logDebug(("[INIT] Got HWND=" + std::to_string(reinterpret_cast<uintptr_t>(m_hWnd))).c_str());
            // Store pointers for subclass proc
            g_subclassDropPath = &m_dropPath;
            g_subclassWin = &win;
            SetPropW(m_hWnd, L"DROP_PATH_PTR", &m_dropPath);
            // Subclass main window and send message to set up drag-drop on webui thread
            WNDPROC oldMain = (WNDPROC)SetWindowLongPtrW(m_hWnd, GWLP_WNDPROC, (LONG_PTR)MainWndSubclassProc);
            SetPropW(m_hWnd, L"OLD_MAIN_PROC", oldMain);
            // SendMessage will be processed by the webui thread's message loop
            // Our MainWndSubclassProc will handle WM_SETUP_DRAGDROP and set up OLE/DragAcceptFiles
            SendMessageW(m_hWnd, WM_SETUP_DRAGDROP, 0, 0);
            logDebug("[INIT] Drag-drop setup complete");
        }

        for (int i = 0; i < 200 && !iconApplied; i++) {
            setWindowIcon();
            if (!iconApplied) {
                webui_wait_async();
            }
        }

        while (true) {
            if (closeRequested && !forceClosing) {
                closeRequested = false;
                char result[8] = {0};
                bool ok = win.script("has_unsaved_changes()", 120000, result, sizeof(result));
                bool hasChanges = ok && strcmp(result, "1") == 0;
                if (!ok || hasChanges) {
                    int ret = MessageBoxA(NULL,
                        "You have unsaved changes.\nLeave without saving?",
                        "Unsaved Changes", MB_YESNO | MB_ICONWARNING | MB_SYSTEMMODAL);
                    if (ret == IDYES) {
                        forceClosing = true;
                        win.run("window.onbeforeunload = null;");
                        webui_exit();
                    }
                } else {
                    forceClosing = true;
                    win.run("window.onbeforeunload = null;");
                    webui_exit();
                }
            }
            if (!webui_wait_async()) break;
            Sleep(50);
        }
    }

    void runBackground() {
        std::string webPath = resolveWebPath();
        logDebug(("[BACKEND] Serving web folder: " + webPath).c_str());
        logDebug(("[BACKEND] JSON root dir: " + rootDir).c_str());
        logDebug("[BACKEND] Mode: directory-browser (multi-client)");

        // Enable multi-client mode so multiple viewers can connect
        webui_set_config(multi_client, true);

        // Use fixed port 9391 (agreed between viewer and backend)
        // Port is now fixed at 9391 — see shared.h
        if (!win.set_port(JSONEDITOR_PORT)) {
            logDebug(("[BACKEND] Port " + std::to_string(JSONEDITOR_PORT) + " is in use, letting WebUI pick").c_str());
        }

        win.set_root_folder(webPath);

        // Start server headlessly
        std::string_view url = win.start_server("");
        if (url.empty()) {
            logDebug("[BACKEND] Failed to start HTTP server!");
            return;
        }

        size_t actualPort = win.get_port();
        logDebug(("[BACKEND] Server running at http://127.0.0.1:" + std::to_string(actualPort)).c_str());

        logDebug("[BACKEND] Entering main loop");
        while (true) {
            if (!webui_wait_async())
                Sleep(100);
        }
    }

private:
    static bool forceClosing;
    static bool closeRequested;

    static bool closeHandler(size_t window) {
        if (forceClosing) return true;
        closeRequested = true;
        return false;
    }

    webui::window win;
    std::string rootDir;
    std::string theme = "dark";
    std::string configPath;
    std::vector<std::pair<std::string, std::string>> jsonFiles;
    bool m_singleFileMode = false;
    std::string m_singleFilePath;
    bool iconApplied = false;
    HWND m_hWnd = NULL;
    FileDropTarget* m_dropTarget = NULL;
    std::string m_dropPath;

    std::string resolveWebPath() {
        std::string exeDir = getExeDir();
        std::string webPath = exeDir + "\\web";

        if (fs::exists(webPath)) {
            return webPath;
        }

        std::string srcWeb = WEB_SOURCE_DIR;
        if (fs::exists(srcWeb)) {
            return srcWeb;
        }

        return "web";
    }

    void setWindowIcon() {
        if (iconApplied) return;
        HWND hwnd = (HWND)win.win32_get_hwnd();
        if (!hwnd) return;

        HMODULE hInst = GetModuleHandleA(NULL);

        HICON hIcon = (HICON)LoadImageA(hInst, MAKEINTRESOURCE(101), IMAGE_ICON,
            0, 0, LR_DEFAULTSIZE);
        HICON hIconSmall = (HICON)LoadImageA(hInst, MAKEINTRESOURCE(101), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

        if (hIcon) {
            SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SetClassLongPtrA(hwnd, GCLP_HICON, (LONG_PTR)hIcon);
        }
        if (hIconSmall) {
            SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
            SetClassLongPtrA(hwnd, GCLP_HICONSM, (LONG_PTR)hIconSmall);
        }

        SetWindowTextA(hwnd, "JsonPilot");
        iconApplied = true;
    }

    static std::string getExeDir() {
        char path[4096];
        GetModuleFileNameA(NULL, path, sizeof(path));
        std::string exePath(path);
        size_t pos = exePath.find_last_of("\\/");
        if (pos != std::string::npos) {
            return exePath.substr(0, pos);
        }
        return ".";
    }

    void loadConfig() {
        std::string exeDir = getExeDir();
        std::vector<std::string> candidates = {
            exeDir + "\\config.txt",
            "config.txt",
            "../config.txt",
            "../../config.txt"
        };

        for (const auto& path : candidates) {
            std::ifstream f(path);
            if (f.is_open()) {
                configPath = path;
                std::string line;
                while (std::getline(f, line)) {
                    if (line.empty() || line[0] == '#') continue;
                    size_t eq = line.find('=');
                    if (eq != std::string::npos) {
                        std::string key = line.substr(0, eq);
                        std::string val = line.substr(eq + 1);
                        trim(key);
                        trim(val);
                        if (key == "root_dir") {
                            rootDir = val;
                        } else if (key == "theme") {
                            if (val == "light" || val == "dark") theme = val;
                        }
                    }
                }
                f.close();
                break;
            }
        }

        if (rootDir.empty()) {
            rootDir = "data";
        }

        if (!fs::u8path(rootDir).is_absolute()) {
            fs::path base = fs::path(configPath).parent_path();
            if (base.empty()) base = fs::current_path();
            fs::path absPath = base / fs::u8path(rootDir);
            rootDir = fs::absolute(absPath).u8string();
        }
    }

    static void trim(std::string& s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) {
            return !std::isspace(c);
        }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) {
            return !std::isspace(c);
        }).base(), s.end());
    }

    void scanJsonFiles() {
        jsonFiles.clear();
        if (!fs::exists(fs::u8path(rootDir))) {
            std::cerr << "Root dir does not exist: " << rootDir << std::endl;
            return;
        }
        scanDir(fs::u8path(rootDir), "");
    }

    void scanDir(const fs::path& dir, const std::string& relativePath) {
        try {
            for (const auto& entry : fs::directory_iterator(dir)) {
                std::string rel = relativePath.empty()
                    ? entry.path().filename().u8string()
                    : relativePath + "/" + entry.path().filename().u8string();

                if (entry.is_directory()) {
                    jsonFiles.emplace_back(rel + "/", std::string(""));
                    scanDir(entry.path(), rel);
                } else if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().u8string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".json") {
                        jsonFiles.emplace_back(rel, entry.path().u8string());
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error scanning directory: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown error scanning directory" << std::endl;
        }
    }

    void bindShowSaveDialog(webui::window::event* e) {
        std::string result;
        std::string currentPath = e->get_string(0);
        // Use parent directory of current file as default folder if available
        std::string initDir;
        if (!currentPath.empty()) {
            fs::path p = fs::u8path(currentPath);
            if (p.has_parent_path()) initDir = p.parent_path().u8string();
        }
        if (initDir.empty()) initDir = rootDir;

        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            IFileSaveDialog* pDlg = NULL;
            hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_ALL,
                                  IID_IFileSaveDialog, (void**)&pDlg);
            if (SUCCEEDED(hr)) {
                COMDLG_FILTERSPEC fileTypes[] = { { L"JSON Files", L"*.json" }, { L"All Files", L"*.*" } };
                pDlg->SetFileTypes(2, fileTypes);
                pDlg->SetDefaultExtension(L"json");
                pDlg->SetTitle(L"Save JSON File");
                if (!initDir.empty()) {
                    std::wstring wRoot = fs::u8path(initDir).wstring();
                    IShellItem* pFolder = NULL;
                    HRESULT hr2 = SHCreateItemFromParsingName(wRoot.c_str(), NULL, IID_PPV_ARGS(&pFolder));
                    if (SUCCEEDED(hr2) && pFolder) {
                        pDlg->SetFolder(pFolder);
                        pFolder->Release();
                    }
                }
                hr = pDlg->Show(NULL);
                if (SUCCEEDED(hr)) {
                    IShellItem* pItem = NULL;
                    hr = pDlg->GetResult(&pItem);
                    if (SUCCEEDED(hr) && pItem) {
                        LPWSTR pszPath = NULL;
                        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                        if (SUCCEEDED(hr) && pszPath) {
                            int len = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, NULL, 0, NULL, NULL);
                            if (len > 0) {
                                result.resize(len - 1);
                                WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, &result[0], len, NULL, NULL);
                            }
                            CoTaskMemFree(pszPath);
                        }
                        pItem->Release();
                    }
                }
                pDlg->Release();
            }
            CoUninitialize();
        }
        e->return_string(result);
    }

    void bindGetConfig(webui::window::event* e) {
        json result;
        result["root_dir"] = rootDir;
        result["theme"] = theme;
        result["success"] = true;
        e->return_string(result.dump());
    }

    void bindGetFileTree(webui::window::event* e) {
        if (m_singleFileMode) {
            e->return_string("");
            return;
        }
        scanJsonFiles();
        std::string result;
        for (const auto& [relPath, absPath] : jsonFiles) {
            result += relPath + "\n";
        }
        e->return_string(result);
    }

    void bindLoadFile(webui::window::event* e) {
        json result;
        try {
            std::string path = e->get_string(0);
            std::string absPath;

            if (m_singleFileMode && !m_singleFilePath.empty()) {
                absPath = m_singleFilePath;
                logDebug(("[LOAD] Single-file mode, using: " + absPath).c_str());
            } else {
                absPath = findAbsPath(path);
                if (absPath.empty() && fs::exists(fs::u8path(path))) {
                    absPath = fs::absolute(fs::u8path(path)).u8string();
                    logDebug(("[LOAD] Using absolute path: " + absPath).c_str());
                }
            }

            if (absPath.empty()) {
                result["success"] = false;
                result["error"] = "File not found: " + path;
                e->return_string(result.dump());
                return;
            }

            auto fileSize = fs::file_size(fs::u8path(absPath));
            if (fileSize > 10 * 1024 * 1024) {
                result["success"] = false;
                result["error"] = "File too large (>10 MB)";
                e->return_string(result.dump());
                return;
            }

            std::ifstream f(fs::u8path(absPath), std::ios::binary);
            if (!f.is_open()) {
                result["success"] = false;
                result["error"] = "Cannot open file";
                e->return_string(result.dump());
                return;
            }

            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
            f.close();

            if (content.empty()) {
                result["success"] = false;
                result["error"] = "File is empty";
                e->return_string(result.dump());
                return;
            }

            try {
                json data = json::parse(content);
                result["success"] = true;
                result["data"] = data;
                result["path"] = path;
            } catch (...) {
                result["success"] = true;
                result["invalid_json"] = true;
                result["raw_text"] = content;
                result["path"] = path;
                result["error"] = "Invalid JSON format - showing as plain text";
            }
        } catch (const std::exception& ex) {
            result["success"] = false;
            result["error"] = std::string("Unexpected error: ") + ex.what();
        } catch (...) {
            result["success"] = false;
            result["error"] = "Unknown error reading file";
        }

        e->return_string(result.dump());
    }

    void bindSaveFile(webui::window::event* e) {
        std::string relPath = e->get_string(0);
        std::string content = e->get_string(1);
        std::string absPath = findAbsPath(relPath);

        json result;
        if (absPath.empty()) {
            fs::path p = fs::u8path(relPath);
            if (p.is_absolute()) {
                absPath = relPath;
            } else {
                absPath = (fs::u8path(rootDir) / p).u8string();
            }
        }

        try {
            json parsed = json::parse(content);
            std::ofstream f(fs::u8path(absPath));
            if (f.is_open()) {
                f << parsed.dump(2);
                f.close();
                result["success"] = true;
                result["path"] = relPath;
            } else {
                result["success"] = false;
                result["error"] = "Cannot write file: " + absPath;
            }
        } catch (const json::parse_error& ex) {
            result["success"] = false;
            result["error"] = std::string("Invalid JSON: ") + ex.what();
        }

        e->return_string(result.dump());
    }

    void bindSaveFileAs(webui::window::event* e) {
        std::string filePath = e->get_string(0);
        std::string content = e->get_string(1);

        fs::path savePath(filePath);
        if (!savePath.is_absolute()) {
            savePath = fs::u8path(rootDir) / fs::u8path(filePath);
        }

        json result;
        try {
            json parsed = json::parse(content);
            std::ofstream f(savePath);
            if (f.is_open()) {
                f << parsed.dump(2);
                f.close();
                result["success"] = true;
                result["path"] = savePath.u8string();
                if (!fs::u8path(filePath).is_absolute()) {
                    scanJsonFiles();
                }
            } else {
                result["success"] = false;
                result["error"] = "Cannot write file: " + savePath.u8string();
            }
        } catch (const json::parse_error& ex) {
            result["success"] = false;
            result["error"] = std::string("Invalid JSON: ") + ex.what();
        }

        e->return_string(result.dump());
    }

    void bindSaveRawFile(webui::window::event* e) {
        std::string relPath = e->get_string(0);
        std::string content = e->get_string(1);
        std::string absPath = findAbsPath(relPath);

        json result;
        if (absPath.empty()) {
            absPath = (fs::u8path(rootDir) / fs::u8path(relPath)).u8string();
        }

        std::ofstream f(fs::u8path(absPath));
        if (f.is_open()) {
            f << content;
            f.close();
            result["success"] = true;
            result["path"] = relPath;
        } else {
            result["success"] = false;
            result["error"] = "Cannot write file: " + absPath;
        }

        e->return_string(result.dump());
    }

    void bindBrowseFolder(webui::window::event* e) {
        std::string result;
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            IFileOpenDialog* pDlg = NULL;
            hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
                                  IID_IFileOpenDialog, (void**)&pDlg);
            if (SUCCEEDED(hr)) {
                pDlg->SetOptions(FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
                pDlg->SetTitle(L"Select JSON Data Root Directory");
                if (!rootDir.empty()) {
                    std::wstring wRoot = fs::u8path(rootDir).wstring();
                    IShellItem* pFolder = NULL;
                    HRESULT hr2 = SHCreateItemFromParsingName(wRoot.c_str(), NULL, IID_PPV_ARGS(&pFolder));
                    if (SUCCEEDED(hr2) && pFolder) {
                        pDlg->SetFolder(pFolder);
                        pFolder->Release();
                    }
                }
                hr = pDlg->Show(NULL);
                if (SUCCEEDED(hr)) {
                    IShellItem* pItem = NULL;
                    hr = pDlg->GetResult(&pItem);
                    if (SUCCEEDED(hr) && pItem) {
                        LPWSTR pszPath = NULL;
                        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
                        if (SUCCEEDED(hr) && pszPath) {
                            int len = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, NULL, 0, NULL, NULL);
                            if (len > 0) {
                                result.resize(len - 1);
                                WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, &result[0], len, NULL, NULL);
                            }
                            CoTaskMemFree(pszPath);
                        }
                        pItem->Release();
                    }
                }
                pDlg->Release();
            }
            CoUninitialize();
        }
        e->return_string(result);
    }

    void bindSetRootDir(webui::window::event* e) {
        std::string newRoot = e->get_string(0);
        if (newRoot.empty()) return;

        if (!fs::u8path(newRoot).is_absolute()) {
            fs::path base = fs::path(configPath).parent_path();
            if (base.empty()) base = fs::current_path();
            fs::path absPath = base / fs::u8path(newRoot);
            newRoot = fs::absolute(absPath).u8string();
        }

        rootDir = newRoot;
        saveConfig();
        scanJsonFiles();
        std::string treeData;
        for (const auto& [rel, abs] : jsonFiles) {
            treeData += rel + "\n";
        }
        json result;
        result["success"] = true;
        result["root_dir"] = rootDir;
        result["tree"] = treeData;
        e->return_string(result.dump());
    }

    void bindCreateFolder(webui::window::event* e) {
        json result;
        try {
            std::string parentRelPath = e->get_string(0);
            std::string folderName = e->get_string(1);

            fs::path parentPath = parentRelPath.empty()
                ? fs::u8path(rootDir)
                : fs::u8path(rootDir) / fs::u8path(parentRelPath);
            fs::path newFolder = parentPath / fs::u8path(folderName);

            std::error_code ec;
            bool created = fs::create_directory(newFolder, ec);
            if (created) {
                scanJsonFiles();
                std::string treeData;
                for (const auto& [rel, abs] : jsonFiles) {
                    treeData += rel + "\n";
                }
                result["success"] = true;
                result["tree"] = treeData;
            } else if (fs::exists(newFolder, ec)) {
                result["success"] = false;
                result["error"] = "Folder already exists";
            } else {
                result["success"] = false;
                result["error"] = "Cannot create folder: " + ec.message();
            }
        } catch (const std::exception& ex) {
            result["success"] = false;
            result["error"] = std::string("Error: ") + ex.what();
        } catch (...) {
            result["success"] = false;
            result["error"] = "Unknown error creating folder";
        }
        e->return_string(result.dump());
    }

    void bindCreateFile(webui::window::event* e) {
        json result;
        try {
            std::string parentRelPath = e->get_string(0);
            std::string fileName = e->get_string(1);

            fs::path parentPath = parentRelPath.empty()
                ? fs::u8path(rootDir)
                : fs::u8path(rootDir) / fs::u8path(parentRelPath);
            fs::path newFile = parentPath / fs::u8path(fileName);

            if (fs::exists(newFile)) {
                result["success"] = false;
                result["error"] = "File already exists";
            } else {
                std::ofstream f(newFile);
                if (f.is_open()) {
                    f << "{}";
                    f.close();
                    scanJsonFiles();
                    std::string treeData;
                    for (const auto& [rel, abs] : jsonFiles) {
                        treeData += rel + "\n";
                    }
                    result["success"] = true;
                    result["tree"] = treeData;
                } else {
                    result["success"] = false;
                    result["error"] = "Cannot create file";
                }
            }
        } catch (const std::exception& ex) {
            result["success"] = false;
            result["error"] = std::string("Error: ") + ex.what();
        } catch (...) {
            result["success"] = false;
            result["error"] = "Unknown error creating file";
        }
        e->return_string(result.dump());
    }

    void bindDeletePath(webui::window::event* e) {
        std::string relPath = e->get_string(0);
        json result;

        fs::path targetPath = fs::u8path(rootDir) / fs::u8path(relPath);
        std::error_code ec;
        if (fs::exists(targetPath, ec)) {
            fs::remove_all(targetPath, ec);
            if (!ec) {
                scanJsonFiles();
                std::string treeData;
                for (const auto& [rel, abs] : jsonFiles) {
                    treeData += rel + "\n";
                }
                result["success"] = true;
                result["tree"] = treeData;
            } else {
                result["success"] = false;
                result["error"] = "Cannot delete: " + ec.message();
            }
        } else {
            result["success"] = false;
            result["error"] = "Path not found";
        }

        e->return_string(result.dump());
    }

    void bindRenamePath(webui::window::event* e) {
        std::string relPath = e->get_string(0);
        std::string newName = e->get_string(1);
        json result;

        fs::path oldPath = fs::u8path(rootDir) / fs::u8path(relPath);
        fs::path newPath = oldPath.parent_path() / fs::u8path(newName);

        std::error_code ec;
        fs::rename(oldPath, newPath, ec);
        if (!ec) {
            scanJsonFiles();
            std::string treeData;
            for (const auto& [rel, abs] : jsonFiles) {
                treeData += rel + "\n";
            }
            result["success"] = true;
            result["tree"] = treeData;
        } else if (fs::exists(newPath, ec)) {
            result["success"] = false;
            result["error"] = "A file or folder with that name already exists";
        } else {
            result["success"] = false;
            result["error"] = "Cannot rename: " + ec.message();
        }

        e->return_string(result.dump());
    }

    void bindCopyFile(webui::window::event* e) {
        std::string relPath = e->get_string(0);
        std::string newName = e->get_string(1);
        json result;

        fs::path srcPath = fs::u8path(rootDir) / fs::u8path(relPath);
        fs::path dstPath = srcPath.parent_path() / fs::u8path(newName);

        std::error_code ec;
        if (fs::exists(dstPath, ec)) {
            result["success"] = false;
            result["error"] = "A file with that name already exists";
        } else {
            fs::copy_file(srcPath, dstPath, ec);
            if (!ec) {
                scanJsonFiles();
                std::string treeData;
                for (const auto& [rel, abs] : jsonFiles) {
                    treeData += rel + "\n";
                }
                result["success"] = true;
                result["tree"] = treeData;
            } else {
                result["success"] = false;
                result["error"] = "Cannot copy: " + ec.message();
            }
        }

        e->return_string(result.dump());
    }

    void bindGetDropPath(webui::window::event* e) {
        logDebug(("[FRONTEND] bindGetDropPath called, returning: " + m_dropPath).c_str());
        e->return_string(m_dropPath);
        m_dropPath.clear();
    }

    void bindLog(webui::window::event* e) {
        std::string level = e->get_string(0);
        std::string msg = e->get_string(1);
        logDebug(("[FRONTEND][" + level + "] " + msg).c_str());
    }

    void bindGetAppMode(webui::window::event* e) {
        json result;
        result["singleFile"] = m_singleFileMode;
        result["filePath"] = m_singleFilePath;
        logDebug(("[MODE] get_app_mode: singleFile=" + std::to_string(m_singleFileMode) + " path=" + m_singleFilePath).c_str());
        e->return_string(result.dump());
    }

    void saveConfig() {
        std::ofstream f(configPath);
        if (f.is_open()) {
            f << "root_dir=" << rootDir << "\n";
            f << "theme=" << theme << "\n";
        }
    }

    std::string findAbsPath(const std::string& relPath) {
        for (const auto& [r, abs] : jsonFiles) {
            if (r == relPath) {
                return abs;
            }
        }
        fs::path candidate = fs::u8path(rootDir) / fs::u8path(relPath);
        if (fs::exists(candidate)) {
            return fs::absolute(candidate).u8string();
        }
        return "";
    }
};

bool JsonEditorApp::forceClosing = false;
bool JsonEditorApp::closeRequested = false;

int main() {
    // ── Single-instance protection ──
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "JsonPilotBackend");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        logDebug("[MAIN] Another backend instance is already running, exiting.");
        return 0;
    }

    // Parse command line for --backend (kept for backward compat with viewer)
    std::string filePath;
    bool backendMode = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; i++) {
            int len = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                std::string arg;
                arg.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, &arg[0], len, NULL, NULL);
                if (arg == "--backend") {
                    backendMode = true;
                } else if (!backendMode && filePath.empty() && arg[0] != '-') {
                    filePath = arg;
                }
            }
        }
        LocalFree(argv);
    }

    logDebug("[MAIN] Starting JsonPilotBackend (headless, single-instance)");
    JsonEditorApp app;
    app.runBackground();
    return 0;
}

#if defined(_WIN32)
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow) {
    return main();
}
#endif
