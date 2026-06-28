#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <string>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <sstream>
#include <WebView2.h>
#include "src/shared.h"

namespace fs = std::filesystem;

static void logDebug(const char* msg) {
    std::time_t t = std::time(nullptr);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    std::string line = std::string("[") + buf + "] " + msg + "\n";
    std::ofstream log("viewer_debug.log", std::ios::app);
    if (log.is_open()) { log << line; log.close(); }
}

static constexpr int WINDOW_WIDTH = 1280;
static constexpr int WINDOW_HEIGHT = 800;

static HWND g_hwnd = NULL;
static ICoreWebView2Controller* g_controller = NULL;
static ICoreWebView2* g_webview = NULL;
static std::string g_targetUrl;
static bool g_isLightTheme = false;

// ── Navigation completed handler ──

struct NavHandler : ICoreWebView2NavigationCompletedEventHandler {
    LONG ref = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_ICoreWebView2NavigationCompletedEventHandler ||
            riid == IID_IUnknown) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = NULL; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref);
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) override {
        BOOL isSuccess = FALSE;
        COREWEBVIEW2_WEB_ERROR_STATUS err = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
        if (args) {
            args->get_IsSuccess(&isSuccess);
            args->get_WebErrorStatus(&err);
        }
        logDebug(("[Nav] isSuccess=" + std::to_string(isSuccess) + " err=" + std::to_string(static_cast<int>(err))).c_str());

        if (isSuccess && sender) {
            logDebug("[Nav] navigation successful");
        }
        return S_OK;
    }
};

// ── Title bar theme helpers ──

static void setTitleBarTheme(bool dark) {
    BOOL mode = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &mode, sizeof(mode));
}

static bool loadThemeFromConfig() {
    std::string exeDir = []() {
        char path[4096];
        GetModuleFileNameA(NULL, path, sizeof(path));
        std::string p(path);
        size_t pos = p.find_last_of("\\/");
        return pos != std::string::npos ? p.substr(0, pos) : ".";
    }();
    std::string cfgPath = exeDir + "\\config.json";
    std::ifstream f(cfgPath);
    if (!f.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    auto pos = content.find("\"theme\"");
    if (pos == std::string::npos) return false;
    pos = content.find('"', pos + 7);
    if (pos == std::string::npos) return false;
    pos++;
    auto end = content.find('"', pos);
    if (end == std::string::npos) return false;
    return content.substr(pos, end - pos) == "light";
}

// ── WebMessage handler (page → host) ──

struct WebMsgHandler : ICoreWebView2WebMessageReceivedEventHandler {
    LONG ref = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_ICoreWebView2WebMessageReceivedEventHandler || riid == IID_IUnknown) { *ppv = this; AddRef(); return S_OK; }
        *ppv = NULL; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&ref); if (r==0) delete this; return r; }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        LPWSTR msg = NULL;
        if (args && SUCCEEDED(args->TryGetWebMessageAsString(&msg)) && msg) {
            std::wstring w(msg);
            std::string s(w.begin(), w.end());
            CoTaskMemFree(msg);
            if (s == "theme:light") { setTitleBarTheme(false); g_isLightTheme = true; }
            else if (s == "theme:dark") { setTitleBarTheme(true); g_isLightTheme = false; }
        }
        return S_OK;
    }
};

// ── Controller completed handler ──

struct ControllerHandler : ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    LONG ref = 1;
    std::wstring url;

    ControllerHandler(const std::wstring& u) : url(u) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler ||
            riid == IID_IUnknown) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = NULL; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref);
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        logDebug("[CtorHandler] Invoke called");
        if (FAILED(result) || !controller) {
            logDebug(("[CtorHandler] FAILED result=0x" + std::to_string(static_cast<long>(result))).c_str());
            return result;
        }
        g_controller = controller;
        controller->AddRef();

        controller->get_CoreWebView2(&g_webview);
        logDebug("[CtorHandler] got webview");

        RECT r;
        GetClientRect(g_hwnd, &r);
        controller->put_Bounds(r);
        // Set theme-appropriate background to prevent flash before page loads
        {
            ICoreWebView2Controller2* ctrl2 = NULL;
            if (SUCCEEDED(controller->QueryInterface(IID_ICoreWebView2Controller2, (void**)&ctrl2)) && ctrl2) {
                COREWEBVIEW2_COLOR bgColor = g_isLightTheme
                    ? COREWEBVIEW2_COLOR{ 255, 255, 255, 255 }
                    : COREWEBVIEW2_COLOR{ 255, 30, 30, 30 };
                ctrl2->put_DefaultBackgroundColor(bgColor);
                ctrl2->Release();
            }
        }
        logDebug("[CtorHandler] bounds set, bg color set");

        // Register NavigationCompleted
        NavHandler* navHandler = new NavHandler();
        navHandler->AddRef();
        EventRegistrationToken token = {};
        g_webview->add_NavigationCompleted(navHandler, &token);
        logDebug("[CtorHandler] Nav handler registered");

        // Log the URL we're navigating to
        std::string urlA(url.begin(), url.end());
        logDebug(("[CtorHandler] Navigating to: " + urlA).c_str());
        g_webview->Navigate(url.c_str());
        logDebug("[CtorHandler] Navigate called");

        ICoreWebView2Settings* settings = NULL;
        g_webview->get_Settings(&settings);
        if (settings) {
            settings->put_IsScriptEnabled(TRUE);
#ifdef JSONEDITOR_DEVTOOLS
            settings->put_AreDevToolsEnabled(TRUE);
#endif
            settings->Release();
        }
        logDebug("[CtorHandler] settings configured");

#ifdef JSONEDITOR_DEVTOOLS
        g_webview->OpenDevToolsWindow();
        logDebug("[CtorHandler] DevTools opened");
#endif

        // Register WebMessage handler (page → host for theme changes)
        WebMsgHandler* msgHandler = new WebMsgHandler();
        msgHandler->AddRef();
        EventRegistrationToken msgToken = {};
        g_webview->add_WebMessageReceived(msgHandler, &msgToken);
        msgHandler->Release();
        logDebug("[CtorHandler] WebMessage handler registered");

        SetForegroundWindow(g_hwnd);
        logDebug("[CtorHandler] done");
        return S_OK;
    }
};

// ── Environment completed handler ──

struct EnvHandler : ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    LONG ref = 1;
    std::wstring url;

    EnvHandler(const std::wstring& u) : url(u) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler ||
            riid == IID_IUnknown) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = NULL; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref);
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override {
        logDebug("[EnvHandler] Invoke called");
        if (FAILED(result) || !env) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                "WebView2 environment creation failed: 0x%08X\n"
                "Please ensure WebView2 Runtime is installed.", result);
            logDebug(("[EnvHandler] FAILED: " + std::string(buf)).c_str());
            MessageBoxA(g_hwnd, buf, "JsonPilot", MB_OK | MB_ICONERROR);
            return result;
        }
        logDebug("[EnvHandler] OK, creating controller...");
        logDebug(("[EnvHandler] URL=" + std::string(url.begin(), url.end())).c_str());
        ControllerHandler* ctrlHandler = new ControllerHandler(url);
        ctrlHandler->AddRef();

        // Try to set theme background before controller creation (avoids white flash)
        ICoreWebView2Environment10* env10 = NULL;
        if (SUCCEEDED(env->QueryInterface(IID_ICoreWebView2Environment10, (void**)&env10)) && env10) {
            ICoreWebView2ControllerOptions* options = NULL;
            if (SUCCEEDED(env10->CreateCoreWebView2ControllerOptions(&options)) && options) {
                ICoreWebView2ControllerOptions3* opts3 = NULL;
                if (SUCCEEDED(options->QueryInterface(IID_ICoreWebView2ControllerOptions3, (void**)&opts3)) && opts3) {
                    COREWEBVIEW2_COLOR bg = g_isLightTheme
                        ? COREWEBVIEW2_COLOR{ 255, 255, 255, 255 }
                        : COREWEBVIEW2_COLOR{ 255, 30, 30, 30 };
                    opts3->put_DefaultBackgroundColor(bg);
                    env10->CreateCoreWebView2ControllerWithOptions(g_hwnd, opts3, ctrlHandler);
                    opts3->Release();
                } else {
                    env10->CreateCoreWebView2ControllerWithOptions(g_hwnd, options, ctrlHandler);
                }
                options->Release();
            } else {
                env10->CreateCoreWebView2ControllerWithOptions(g_hwnd, NULL, ctrlHandler);
            }
            env10->Release();
        } else {
            env->CreateCoreWebView2Controller(g_hwnd, ctrlHandler);
        }
        ctrlHandler->Release();
        logDebug("[EnvHandler] CreateCoreWebView2Controller returned");
        return S_OK;
    }
};

// ── Helpers ──

static std::string getExeDir() {
    char path[4096];
    GetModuleFileNameA(NULL, path, sizeof(path));
    std::string exePath(path);
    size_t pos = exePath.find_last_of("\\/");
    return (pos != std::string::npos) ? exePath.substr(0, pos) : ".";
}

static bool isBackendProcessRunning() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = { sizeof(pe) };
    bool found = false;
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"JsonPilotBackend.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return found;
}

static bool startBackendProcess() {
    std::string exeDir = getExeDir();
    std::string backendPath = exeDir + "\\JsonPilotBackend.exe";
    if (!fs::exists(fs::u8path(backendPath)))
        return false;

    SHELLEXECUTEINFOA sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpFile = backendPath.c_str();
    sei.lpParameters = "--backend";
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExA(&sei)) return false;
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return true;
}

typedef HRESULT (__stdcall *CreateCoreWebView2EnvironmentWithOptionsFunc)(
    PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

static void initWebView(HWND hwnd) {
    logDebug("[initWebView] Starting...");
    logDebug(("[initWebView] URL=" + g_targetUrl).c_str());
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    logDebug("[initWebView] CoInitializeEx done");

    HMODULE wv2Lib = LoadLibraryA("WebView2Loader.dll");
    if (!wv2Lib) {
        logDebug("[initWebView] FAILED LoadLibrary(WebView2Loader.dll)");
        MessageBoxA(hwnd,
            "WebView2Loader.dll not found.\n"
            "Please install WebView2 Runtime.",
            "JsonPilot", MB_OK | MB_ICONERROR);
        return;
    }
    logDebug("[initWebView] LoadLibrary OK");

    auto createEnvFn = (CreateCoreWebView2EnvironmentWithOptionsFunc)
        GetProcAddress(wv2Lib, "CreateCoreWebView2EnvironmentWithOptions");
    if (!createEnvFn) {
        logDebug("[initWebView] FAILED GetProcAddress");
        FreeLibrary(wv2Lib);
        MessageBoxA(hwnd,
            "Incompatible WebView2Loader.dll version.",
            "JsonPilot", MB_OK | MB_ICONERROR);
        return;
    }
    logDebug("[initWebView] GetProcAddress OK");

    std::wstring wurl(g_targetUrl.begin(), g_targetUrl.end());
    EnvHandler* envHandler = new EnvHandler(wurl);
    envHandler->AddRef();

    logDebug("[initWebView] Calling CreateCoreWebView2EnvironmentWithOptions...");
    createEnvFn(NULL, NULL, NULL, envHandler);
    envHandler->Release();
    logDebug("[initWebView] CreateCoreWebView2EnvironmentWithOptions returned");
    // Keep wv2Lib loaded — async callbacks may still reference it
}

// ── Window Proc ──

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            if (g_controller) {
                RECT r;
                GetClientRect(hwnd, &r);
                g_controller->put_Bounds(r);
            }
            return 0;
        case WM_DESTROY:
            if (g_controller) { g_controller->Release(); g_controller = NULL; }
            if (g_webview) { g_webview->Release(); g_webview = NULL; }
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND: {
            HDC dc = (HDC)wParam;
            RECT r;
            GetClientRect(hwnd, &r);
            HBRUSH brush = CreateSolidBrush(g_isLightTheme ? RGB(255, 255, 255) : RGB(30, 30, 30));
            FillRect(dc, &r, brush);
            DeleteObject(brush);
            return 1;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Entry Point ──

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, PSTR cmdLine, int showCmd) {
    (void)cmdLine; (void)showCmd;

    // Enable per-monitor DPI awareness for crisp text rendering
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Parse command line for file path, resolving relative paths to absolute
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::string filePath;
    if (argv && argc > 1) {
        std::wstring ws(argv[1]);
        if (!ws.empty() && ws[0] != L'-') {
            wchar_t absPathW[MAX_PATH];
            DWORD absLen = GetFullPathNameW(ws.c_str(), MAX_PATH, absPathW, NULL);
            if (absLen > 0 && absLen < MAX_PATH) {
                int len = WideCharToMultiByte(CP_UTF8, 0, absPathW, -1, NULL, 0, NULL, NULL);
                if (len > 0) {
                    filePath.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, absPathW, -1, &filePath[0], len, NULL, NULL);
                }
            }
        }
    }
    if (argv) LocalFree(argv);

    // Use fixed port 9391 (agreed between viewer and backend)
    constexpr int port = JSONEDITOR_PORT;

    // Check if backend process is already running
    if (!isBackendProcessRunning()) {
        if (!startBackendProcess()) {
            MessageBoxA(NULL,
                "Failed to start JsonPilot backend.\n"
                "Please restart the application.",
                "JsonPilot", MB_OK | MB_ICONERROR);
            return 1;
        }
        // Give the backend a moment to start up
        Sleep(1500);
    }

    // Build target URL
    g_targetUrl = "http://127.0.0.1:" + std::to_string(port) + "/index.html";
    if (!filePath.empty()) {
        g_targetUrl += "?file=" + urlEncode(filePath);
    }
    logDebug(("[MAIN] Target URL: " + g_targetUrl).c_str());

    // Register window class
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101),
                    IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                    GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"JsonPilotViewer";
    RegisterClassExW(&wc);

    // Read theme from config.json
    g_isLightTheme = loadThemeFromConfig();

    // Create window (shown before WebView2 init so the parent is visible)
    RECT wr = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    int winX = (GetSystemMetrics(SM_CXSCREEN) - (wr.right - wr.left)) / 2;
    int winY = (GetSystemMetrics(SM_CYSCREEN) - (wr.bottom - wr.top)) / 2;

    g_hwnd = CreateWindowExW(0, L"JsonPilotViewer", L"JsonPilot",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, winX, winY,
        wr.right - wr.left, wr.bottom - wr.top,
        NULL, NULL, hInstance, NULL);
    if (!g_hwnd) {
        MessageBoxA(NULL, "Failed to create window.", "JsonPilot", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Title bar theme (must be after CreateWindow so g_hwnd is valid)
    setTitleBarTheme(!g_isLightTheme);

    // Initialize WebView2 (async, shows window when ready)
    initWebView(g_hwnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}
