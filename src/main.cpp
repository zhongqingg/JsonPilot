#include "webui.hpp"
#include "json.hpp"
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
        win.bind("get_config", this, &JsonEditorApp::bindGetConfig);
        win.bind("show_save_dialog", this, &JsonEditorApp::bindShowSaveDialog);
        win.set_close_handler_wv(closeHandler);
    }

    void run() {
        std::string webPath = resolveWebPath();
        std::cout << "Serving web folder: " << webPath << std::endl;
        std::cout << "JSON root dir: " << rootDir << std::endl;

        win.set_root_folder(webPath);
        win.show("");

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
    std::vector<std::pair<std::string, std::string>> jsonFiles;

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

        if (!fs::path(rootDir).is_absolute()) {
            fs::path base = fs::path(configPath).parent_path();
            if (base.empty()) base = fs::current_path();
            fs::path absPath = base / rootDir;
            rootDir = fs::absolute(absPath).string();
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
        if (!fs::exists(rootDir)) {
            std::cerr << "Root dir does not exist: " << rootDir << std::endl;
            return;
        }
        scanDir(rootDir, "");
    }

    void scanDir(const fs::path& dir, const std::string& relativePath) {
        try {
            for (const auto& entry : fs::directory_iterator(dir)) {
                std::string rel = relativePath.empty()
                    ? entry.path().filename().string()
                    : relativePath + "/" + entry.path().filename().string();

                if (entry.is_directory()) {
                    scanDir(entry.path(), rel);
                } else if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".json") {
                        jsonFiles.emplace_back(rel, entry.path().string());
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Error scanning directory: " << e.what() << std::endl;
        }
    }

    json buildFileTreeJson() {
        json tree = json::object();
        for (const auto& [relPath, absPath] : jsonFiles) {
            json* current = &tree;
            fs::path p(relPath);
            std::string accum;
            for (const auto& part : p) {
                std::string partStr = part.string();
                if (!accum.empty()) accum += "/";
                accum += partStr;
                if (part == p.filename()) {
                    if (!current->is_object()) {
                        *current = json::object();
                    }
                    (*current)[partStr] = json{{"file", relPath}};
                } else {
                    if (!current->is_object()) {
                        *current = json::object();
                    }
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
                    std::wstring wRoot = fs::path(rootDir).wstring();
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
        json tree = buildFileTreeJson();
        e->return_string(tree.dump());
    }

    void bindLoadFile(webui::window::event* e) {
        std::string relPath = e->get_string(0);
        std::string absPath = findAbsPath(relPath);

        json result;
        if (absPath.empty()) {
            result["success"] = false;
            result["error"] = "File not found: " + relPath;
        } else {
            try {
                std::ifstream f(absPath);
                if (f.is_open()) {
                    json data = json::parse(f);
                    result["success"] = true;
                    result["data"] = data;
                    result["path"] = relPath;
                    f.close();
                } else {
                    result["success"] = false;
                    result["error"] = "Cannot open file: " + absPath;
                }
            } catch (const json::parse_error& ex) {
                result["success"] = false;
                result["error"] = std::string("JSON parse error: ") + ex.what();
            }
        }

        e->return_string(result.dump());
    }

    void bindSaveFile(webui::window::event* e) {
        std::string relPath = e->get_string(0);
        std::string content = e->get_string(1);
        std::string absPath = findAbsPath(relPath);

        json result;
        if (absPath.empty()) {
            absPath = (fs::path(rootDir) / relPath).string();
        }

        try {
            json parsed = json::parse(content);
            std::ofstream f(absPath);
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
            savePath = fs::path(rootDir) / filePath;
        }

        json result;
        try {
            json parsed = json::parse(content);
            std::ofstream f(savePath.string());
            if (f.is_open()) {
                f << parsed.dump(2);
                f.close();
                result["success"] = true;
                result["path"] = savePath.string();
                if (!fs::path(filePath).is_absolute()) {
                    scanJsonFiles();
                }
            } else {
                result["success"] = false;
                result["error"] = "Cannot write file: " + savePath.string();
            }
        } catch (const json::parse_error& ex) {
            result["success"] = false;
            result["error"] = std::string("Invalid JSON: ") + ex.what();
        }

        e->return_string(result.dump());
    }

    std::string findAbsPath(const std::string& relPath) {
        for (const auto& [r, abs] : jsonFiles) {
            if (r == relPath) {
                return abs;
            }
        }
        fs::path candidate = fs::path(rootDir) / relPath;
        if (fs::exists(candidate)) {
            return fs::absolute(candidate).string();
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
