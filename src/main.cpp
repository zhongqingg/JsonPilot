#include "webui.hpp"
#include "nlohmann/json.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <windows.h>
#include <shobjidl.h>
#include <comdef.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

class JsonEditorApp {
public:
    JsonEditorApp() {
        loadConfig();
        scanJsonFiles();

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
        win.set_close_handler_wv(closeHandler);
    }

    void run() {
        std::string webPath = resolveWebPath();
        std::cout << "Serving web folder: " << webPath << std::endl;
        std::cout << "JSON root dir: " << rootDir << std::endl;

        win.set_root_folder(webPath);
        if (!win.show_wv("")) {
            win.show("");
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
    std::string logPath;
    std::vector<std::pair<std::string, std::string>> jsonFiles;
    bool iconApplied = false;

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

    void logLine(const std::string& message) {
        if (logPath.empty()) {
            logPath = getExeDir() + "\\JsonPilot.log";
        }
        std::ofstream log(logPath, std::ios::app | std::ios::binary);
        if (log.is_open()) {
            log << message << "\n";
        }
    }

    void loadConfig() {
        std::string exeDir = getExeDir();
        logPath = exeDir + "\\JsonPilot.log";
        {
            std::ofstream log(logPath, std::ios::trunc | std::ios::binary);
            if (log.is_open()) {
                log << "JsonPilot debug log\n";
                log << "exe_dir=" << exeDir << "\n";
            }
        }
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
        logLine("config_path=" + configPath);
        logLine("root_dir=" + rootDir);
        logLine("theme=" + theme);
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
        logLine("scan_start root_dir=" + rootDir);
        if (!fs::exists(fs::u8path(rootDir))) {
            logLine("scan_error root dir does not exist");
            return;
        }
        scanDir(fs::u8path(rootDir), "");
        logLine("scan_done json_count=" + std::to_string(jsonFiles.size()));
        for (const auto& [relPath, absPath] : jsonFiles) {
            logLine("json_file rel=" + relPath + " abs=" + absPath);
        }
    }

    void scanDir(const fs::path& dir, const std::string& relativePath) {
        try {
            for (const auto& entry : fs::directory_iterator(dir)) {
                std::string rel = relativePath.empty()
                    ? entry.path().filename().u8string()
                    : relativePath + "/" + entry.path().filename().u8string();

                if (entry.is_directory()) {
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

    json buildFileTreeJson() {
        json tree = json::object();
        for (const auto& [relPath, absPath] : jsonFiles) {
            json* current = &tree;
            // Split by '/' manually (avoid fs::path iterator issues)
            std::vector<std::string> parts;
            size_t start = 0, pos;
            while ((pos = relPath.find('/', start)) != std::string::npos) {
                parts.push_back(relPath.substr(start, pos - start));
                start = pos + 1;
            }
            parts.push_back(relPath.substr(start));

            for (size_t i = 0; i < parts.size(); i++) {
                const std::string& partStr = parts[i];
                if (!current->is_object()) {
                    *current = json::object();
                }
                if (i == parts.size() - 1) {
                    (*current)[partStr] = json{{"file", relPath}};
                } else {
                    if (!current->contains(partStr)) {
                        (*current)[partStr] = json::object();
                    }
                    auto& next = (*current)[partStr];
                    if (next.is_object() && next.contains("file")) {
                        json old = next;
                        next = json::object();
                        next["__files__"] = old;
                    }
                    current = &(*current)[partStr];
                }
            }
        }
        return tree;
    }

    void bindShowSaveDialog(webui::window::event* e) {
        std::string result;
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

    void bindGetConfig(webui::window::event* e) {
        json result;
        result["root_dir"] = rootDir;
        result["theme"] = theme;
        result["success"] = true;
        e->return_string(result.dump());
    }

    void bindGetFileTree(webui::window::event* e) {
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
            std::string relPath = e->get_string(0);
            std::string absPath = findAbsPath(relPath);

            if (absPath.empty()) {
                result["success"] = false;
                result["error"] = "File not found: " + relPath;
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
                result["path"] = relPath;
            } catch (...) {
                result["success"] = true;
                result["invalid_json"] = true;
                result["raw_text"] = content;
                result["path"] = relPath;
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
            absPath = (fs::u8path(rootDir) / fs::u8path(relPath)).u8string();
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
        json tree = buildFileTreeJson();
        json result;
        result["success"] = true;
        result["root_dir"] = rootDir;
        result["tree"] = tree;
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
                json tree = buildFileTreeJson();
                result["success"] = true;
                result["tree"] = tree;
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
                    json tree = buildFileTreeJson();
                    result["success"] = true;
                    result["tree"] = tree;
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
                json tree = buildFileTreeJson();
                result["success"] = true;
                result["tree"] = tree;
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
            json tree = buildFileTreeJson();
            result["success"] = true;
            result["tree"] = tree;
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
                json tree = buildFileTreeJson();
                result["success"] = true;
                result["tree"] = tree;
            } else {
                result["success"] = false;
                result["error"] = "Cannot copy: " + ec.message();
            }
        }

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
    JsonEditorApp app;
    app.run();
    return 0;
}

#if defined(_WIN32)
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow) {
    return main();
}
#endif
