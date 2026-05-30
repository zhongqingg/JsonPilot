# JsonPilot

<p align="center">
  <img src="src/icon.png" alt="JsonPilot Icon" width="128" height="128">
</p>

<p align="center"><strong>Navigate and Edit JSON with Precision</strong></p>

<p align="center">
  <em>A fast, native desktop JSON editor with a clean tree-based UI, built for developers who work with large structured JSON files every day.</em>
</p>

---

## Features

**Tree-based JSON Editor** — Browse and edit deeply nested JSON structures in a collapsible tree view. No more scrolling through raw text in a plain editor. Double-click any key or value to edit inline.

**Multi-file Workspace** — Point the app at a root directory and it scans all `.json` files recursively. The sidebar shows a full file tree, making it easy to switch between related configuration files, data exports, or API responses.

**Undo/Redo** — Full undo/redo stack (up to 50 levels). Every edit — key rename, value change, node deletion, duplication, replace — is tracked. Work fearlessly.

**Search & Replace** — Find any key, value, or string across the entire JSON document. Replace in a specific scope (current node, current value) or globally. All matches are highlighted in real time.

**Change Tracking (Diff)** — Visual diff markers highlight added, modified, and deleted nodes. See exactly what changed since the last save.

**Dark & Light Themes** — Toggle between a dark theme (default, for long editing sessions) and a light theme with a single click.

**Drag & Drop** — Drag any `.json` file from your file explorer directly into the editor window to open it.

**Save As with Native Dialog** — Save files anywhere on your filesystem using the native Windows save dialog, or type a relative path to save within the workspace root.

**Add Child Items** — Right-click any object or array node to add new children with arbitrary JSON values. Supports both object keys and array elements.

**Duplicate & Delete** — Right-click any node to duplicate it (auto-renames to avoid conflicts) or delete it. Deleted items leave a visual marker so you can see what was removed.

**Unsaved Changes Protection** — Closing the window with unsaved edits prompts a confirmation dialog.

## Screenshot

<p align="center">
  <img src="src/icon.png" alt="JsonPilot" width="256">
</p>

> *A native desktop application — clean, fast, and built with a web-based UI powered by WebUI.*

## Build Environment

### Prerequisites

| Component | Version | Notes |
|-----------|---------|-------|
| **CMake** | 3.18+ | Build system |
| **Visual Studio** | 2022 | With C++ Desktop Development workload (MSVC toolchain) |
| **Windows SDK** | 10.0+ | Required for WebView2 support |
| **WebView2 Runtime** | Any | Pre-installed on Windows 10/11; the app embeds WebView2 via the WebUI library |

### Third-party Libraries (included in `thirdparty/`)

| Library | Version | Purpose |
|---------|---------|---------|
| [WebUI](https://github.com/webui-dev/webui) | 2.5.0-beta | Cross-platform web UI library for C++. Uses the system's native WebView (Edge WebView2 on Windows) to render HTML/CSS/JS as the app interface. No bundled browser — lightweight and fast startup. |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | JSON for Modern C++. Header-only library for parsing, manipulating, and serializing JSON with an intuitive STL-like API. |

## Project Structure

```
JsonPilot/
├── CMakeLists.txt          # CMake build configuration
├── src/
│   ├── main.cpp            # C++ backend: file I/O, window management, WebUI bindings
│   ├── resources.rc.in     # Windows resource template (icon, version info)
│   ├── icon.ico            # Application icon (multi-resolution Windows ICO)
│   └── icon.png            # Application icon (high-resolution PNG)
├── web/
│   ├── index.html          # Main UI layout (sidebar, toolbar, editor)
│   ├── script.js           # Frontend logic: tree rendering, editing, search, undo/redo
│   └── style.css           # Complete stylesheet with dark/light theme support
├── thirdparty/
│   ├── webui/              # WebUI library (includes WebView2 integration)
│   └── nlohmannjson/       # nlohmann JSON library (header-only)
├── data/                   # Default JSON data directory (configurable via config.txt)
├── build/                  # CMake build output directory
└── deploy/                 # Packaging output (created by packaging step)
```

## Building from Source

### Quick Build (Windows)

```powershell
# 1. Clone the repository
git clone https://github.com/zhongqingg/JsonPilot.git
cd JsonPilot

# 2. Configure with CMake (Release)
cmake -B build -S . -G "Visual Studio 17 2022" -A x64

# 3. Build
cmake --build build --config Release

# 4. The executable is at:
#    build\Release\JsonPilot.exe
```

### CMake Configuration Options

```powershell
# Build type: Release (recommended) or Debug
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Specify Visual Studio generator explicitly
cmake -B build -S . -G "Visual Studio 17 2022" -A x64

# For MinGW-w64 (alternative)
cmake -B build -S . -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Build Notes

- The build automatically copies the `web/` folder and icon files to the output directory via `POST_BUILD` commands.
- On Windows, the executable is built as a GUI application (`WIN32` flag in CMake, using `WinMain` entry point — no console window).
- The `resources.rc` file is generated from `resources.rc.in` by CMake's `configure_file`, embedding the icon and version information into the executable.
- `WebUI` library is compiled from source as a CMake subdirectory. It links against `ole32`, `runtimeobject`, and `shell32` on Windows.
- The app uses `C++17` (`std::filesystem` for path operations) and `C99`.

## Usage

1. **Configure the root directory:** Edit `config.txt` next to the executable:
   ```
   root_dir=data
   theme=dark
   ```
   Set `root_dir` to an absolute or relative path containing your JSON files.

2. **Launch `JsonPilot.exe`** — the file tree loads automatically.

3. **Select a file** from the sidebar to open it in the tree editor.

4. **Edit values** by double-clicking any key or value, type your changes, and press Enter.

5. **Right-click** any node for context menu actions: Add Child, Copy, Duplicate, Delete, Replace.

6. **Search** with `Ctrl+F` to find text across the document. Use `Ctrl+H` for replace.

7. **Save** your changes with the Save button or `Ctrl+S`. Use Save As to write to a new file.

## Packaging & Deployment

To create a distributable package:

```powershell
# Build in Release mode
cmake --build build --config Release

# The deploy folder structure:
# deploy/JsonPilot/
# ├── JsonPilot.exe
# ├── config.txt
# ├── web/
# │   ├── index.html
# │   ├── script.js
# │   ├── style.css
# │   ├── icon.png
# │   └── icon.ico
# └── data/
#     └── (your JSON files)
```

### Using NSIS Installer (recommended)

An NSIS script (`installer.nsi`) can be provided to create a standard Windows installer that:
- Installs to `Program Files\JsonPilot`
- Creates Start Menu and Desktop shortcuts
- Registers an uninstaller

### Portable Distribution

Alternatively, simply zip the `deploy/JsonPilot/` folder — the application is fully self-contained and requires no installation. Just extract and run `JsonPilot.exe`.

## Architecture

JsonPilot uses a **native C++ backend + web frontend** architecture:

- **Backend (C++):** File system access, JSON parsing, memory management, native dialogs. Uses Win32 API for window management and WebUI for rendering.
- **Frontend (HTML/CSS/JS):** Tree UI rendering, inline editing, search/replace, undo/redo. Runs inside the system WebView2 control.
- **Communication:** The C++ backend exposes functions to JavaScript via WebUI bindings (`get_file_tree`, `load_file`, `save_file`, `save_file_as`, `get_config`, `show_save_dialog`). JavaScript calls these as async functions.

This architecture provides the best of both worlds: native file system access and OS integration from C++, with the flexibility and development speed of a web-based UI.

## License

MIT License

---

<p align="center">
  <sub>Built with C++, WebUI, and nlohmann/json</sub>
</p>
