# JsonPilot — Project Record

## Architecture
Dual-process C++ desktop JSON editor using **WebUI** (civetweb HTTP/WS server) + **WebView2**:
- **JsonPilotBackend.exe** — headless HTTP+WebSocket backend, auto-starts at boot (registry), no tray icon, no window
- **JsonPilotViewer.exe** — Win32 window with WebView2 control, launched by user, connects to backend via `http://127.0.0.1:{PORT}`

## Key Facts
- Port range: 12700–12709 (defined in `src/shared.h` as `JSONEDITOR_PORT=12700`, `JSONEDITOR_PORT_RANGE=10`)
- Port file at `%APPDATA%/JsonPilot/port.txt` — IPC between backend and viewer
- Backend uses `webui_start_server()` with `multi_client=true` (multiple concurrent viewer windows)
- Backend uses `NoBrowser` mode — no GUI window, pure HTTP+WS server
- `webui_wait_async()` keeps backend alive (sets `_webui.ui=true` internally)
- File path passed to viewer via URL query param `?file=...` (no HTML injection / `__pilotMode`)
- Viewer checks backend health via `GET /ping` (static file served by WebUI)
- Viewer restarts backend once if ping fails after retries

## Build System
- **CMake** with Visual Studio 17 2022 generator, x64
- Multi-threaded static MSVC runtime (no vcruntime140.dll dependency)
- `deploy/build.bat` — builds both targets, packages to `deploy/JsonPilot/`

## Key Source Files
| File | Purpose |
|------|---------|
| `src/shared.h` | Port constants, `readPortFile()`/`writePortFile()`, `urlEncode()`, `getAppDataPath()` |
| `src/viewer_main.cpp` | Win32 `WinMain()` — WebView2 init, backend ping/detect/restart, dark title bar |
| `src/main.cpp` | `--backend` flag detection, `runBackground()` (headless server); config.json (sessions/theme); file operations |
| `web/script.js` | `init()` with URL param `?file=` parsing; session management (add/delete/expand); renderFileTree |
| `web/index.html` | `<style>html{background:#1e1e1e}body{visibility:hidden}</style>` for flash suppression |
| `web/style.css` | Sidebar session list, file tree, JSON editor, dialog/menu styles |
| `deploy/config.json` | Sessions config: `{"theme":"dark","sessions":[{"name":"...","path":"..."}]}` |
| `thirdparty/webview2/include/WebView2.h` | WebView2 SDK header (C++ COM interface definitions) |
| `thirdparty/WebView2Loader.dll` | WebView2 runtime loader DLL |

## Dependencies
- **WebUI** (MIT, statically linked via `thirdparty/webui`)
- **nlohmann/json** (MIT, header-only via `thirdparty/nlohmannjson`)
- **WebView2** (via `WebView2Loader.dll` + header; loaded dynamically at runtime)

## Session / Project Management
- Config file: `config.json` at exe directory, stores `theme` + `sessions` array
- Session fields: `name` (display name), `path` (directory path)
- Frontend sidebar: session list with expand/collapse, + button to add, ✕ to delete
- Right-click session header → context menu (New Folder, New JSON File in session root)
- Right-click files/folders under session → rename/delete/copy/create
- `get_sessions`, `add_session`, `delete_session`, `get_file_tree_dir` backend APIs
- File operations (`create_folder`, `create_file`, `rename_path`, `delete_path`, `copy_file`) resolve absolute paths via `resolveParentPath()`

## Known Issues / Todo
- Old single-process `JsonPilot.exe` may linger in build/Release; can be deleted
- Viewer re-launches backend if not running; OS launches viewer for `.json` files via file association
- Backend can only be killed via Task Manager (intentional design)
- Backend has single-instance protection via named mutex `JsonPilotBackend`
- Backend always runs headless; never opens a frontend window
- Viewer creates window with `WS_VISIBLE` from the start (flash suppressed by CSS dark background + `body{visibility:hidden}`)
