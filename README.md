# C++ WebView GUI with Vue 3

Modern C++ desktop application using [webview/webview](https://github.com/webview/webview) for cross-platform GUI, [Vue 3](https://vuejs.org/) for the frontend, and [nlohmann/json](https://github.com/nlohmann/json) for JSON parsing.

## ✨ Features

- 🖥️ **Cross-platform** - Windows, macOS, Linux
- ⚡ **Hot Reload** - Vite dev server with instant updates during development
- 🎯 **Modern C++20** - Uses `std::expected`, `std::span`, `std::format`
- 🔧 **CLI Options** - Built-in argument parser with bash completion
- 🛡️ **Sanitizers** - Address, undefined behavior, and leak sanitizers in debug
- 📦 **Smart Dependencies** - Uses system libraries when available, downloads otherwise

## 📁 Project Structure

```
├── CMakeLists.txt          # Build configuration with auto-detection
├── include/
│   ├── option_parser.hpp   # CLI argument parser (combined header)
│   ├── option_parser_decls.hpp  # Parser declarations
│   ├── option_parser_impl.hpp   # Parser implementation
│   └── expected.hpp        # C++20/23 std::expected wrapper
├── src/
│   ├── main.cpp            # Entry point with CLI handling
│   ├── dev_server.h        # Vite dev server management
│   └── app/
│       ├── application.h   # Main Application class
│       ├── bindings.h      # JS ↔ C++ bindings
│       ├── cli_options.h   # CLI option definitions
│       └── config.h        # App configuration
├── ui/                     # Vue 3 frontend
│   ├── index.html
│   ├── package.json
│   ├── vite.config.js
│   ├── postbuild.js        # Generates embedded header for production
│   └── src/
│       ├── App.vue         # Main Vue component
│       ├── main.js         # Vue app entry
│       └── style.css       # Global styles
└── .vscode/
    └── settings.json       # VS Code integration (ASAN env vars)
```

## 🔧 Dependencies

### Required

| Tool | Version |
|------|---------|
| CMake | ≥ 3.16 |
| C++ Compiler | C++20 support (GCC 11+, Clang 14+, MSVC 2022+) |
| Node.js | Latest LTS |
| Ninja | Recommended (faster builds) |

### Platform-specific

| Platform | Requirements |
|----------|-------------|
| **Linux** | WebKitGTK 4.1+ (`sudo apt install libwebkit2gtk-4.1-dev`) |
| **macOS** | WebKit (included in macOS) |
| **Windows** | WebView2 Runtime (included in Windows 11) |

### Auto-detected Libraries

The build system automatically detects and uses system libraries when available:

- **nlohmann/json** - Falls back to FetchContent if not installed
- **WebKitGTK** - Auto-detects version (6.0 → 4.1 → 4.0)

## 🚀 Quick Start

### Development Mode (with Hot Reload)

```bash
# 1. Install UI dependencies (first time only)
cd ui && npm install && cd ..

# 2. Configure for development
cmake -B build -G Ninja -DDEV_MODE=ON

# 3. Build
cmake --build build

# 4. Run the app
./build/bin/app --dev
```

> 💡 **Auto-start Vite**: The app automatically starts the Vite dev server if it's not already running! You don't need to run `npm run dev` manually. The app will:
> 1. Check if Vite is running on port 5173
> 2. Start it automatically if not
> 3. Wait for the server to be ready
> 4. Navigate to the dev server URL
>
> If you prefer to run Vite manually (for seeing its logs), start it in a separate terminal with `cd ui && npm run dev`.

Changes in `ui/src/` will instantly reflect in the running app! 🔥

### Production Build

```bash
# 1. Configure for production
cmake -B build -G Ninja -DDEV_MODE=OFF

# 2. Build (automatically installs npm deps and builds UI)
cmake --build build

# 3. Run
./build/bin/app
```

> 💡 **Auto-build UI**: In production mode, CMake automatically:
> 1. Runs `npm install` to ensure dependencies are installed
> 2. Runs `npm run build` when UI source files change
> 3. Generates `ui/dist/index_html.h` which embeds the entire frontend as a C++ string literal
>
> No manual npm commands needed!

## 📖 CLI Options

```
Usage: app [OPTIONS]

Options:
  -d, --dev                   Force development mode (Vite dev server)
  -p, --prod                  Force production mode (embedded HTML)
  -v, --verbose               Enable verbose logging
  -V, --version               Show version information
  -W, --width <pixels>        Set window width
  -H, --height <pixels>       Set window height
  -u, --url <url>             Navigate to custom URL

  -h, --help                  Show help message
      --help-verbose          Show detailed help

Examples:
  app                         # Auto-detect mode
  app --dev                   # Force development mode
  app --prod                  # Force production mode
  app -W 1920 -H 1080         # Custom window size
  app --url http://localhost:3000  # Custom URL
```

### Bash Completion

The app supports bash completion. When `COMP_LINE` is set, it outputs completions:

```bash
COMP_LINE="app --" COMP_POINT=7 ./build/bin/app
# Output: --dev --prod --verbose --version --width --height --url
```

## ⚙️ CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `DEV_MODE` | `ON` (Debug) | Use Vite dev server instead of embedded HTML |
| `ENABLE_SANITIZERS` | `ON` (Debug) | Enable ASAN/UBSAN/LSAN |
| `ENABLE_WARNINGS` | `ON` | Enable compiler warnings |
| `FETCHCONTENT_QUIET` | `ON` | Set to `OFF` to see FetchContent download progress |

```bash
# Example: Production build without sanitizers
cmake -B build -DDEV_MODE=OFF -DENABLE_SANITIZERS=OFF -DCMAKE_BUILD_TYPE=Release

# Example: Verbose dependency download
cmake -B build -DFETCHCONTENT_QUIET=OFF
```

### Dependency Caching

FetchContent dependencies are cached in `.deps/` to speed up rebuilds:

- **Persistent cache** - Survives `rm -rf build`
- **Shallow clones** - Only downloads the specific tag, not full history
- **System library detection** - Uses `nlohmann_json` from system if available

To force re-download of dependencies:
```bash
rm -rf .deps
cmake -B build
```

## 🏗️ Architecture

### JS ↔ C++ Communication

The frontend communicates with C++ via `window.ping()`:

```javascript
// Vue component
window.ping(message)  // Calls C++ binding
```

```cpp
// C++ binding (bindings.h)
webview.bind("ping", [](const std::string& req) {
    // Handle request from JS
    return response;
});
```

### Dev Server Management

In development mode, `dev_server.h` provides automatic Vite server management:

```cpp
// The app automatically handles the dev server
DevServer server(ui_directory);
server.ensure_running();  // Starts Vite if not running
std::string url = server.get_url();  // http://127.0.0.1:5173
```

**Features:**
- 🔍 **Auto-detection** - Checks if Vite is already running on port 5173
- 🚀 **Auto-start** - Spawns `npm run dev` if server is not running
- ⏳ **Wait for ready** - Polls until the server responds
- 🧹 **Auto-cleanup** - Terminates the server when the app exits (if it started it)

This means you can just run the app and it handles everything!

### Option Parser

Custom CLI parser (`option_parser.hpp`) with:

- Type-safe option definitions
- Short and long options
- Value validation
- Auto-generated help text
- Bash completion support

## 🔍 Debugging

### Running with Sanitizers

Debug builds include Address Sanitizer. To suppress known leaks:

```bash
# Using the wrapper script
./build/bin/run.sh

# Or manually
LSAN_OPTIONS="suppressions=.asan_suppressions" ./build/bin/app
```

### VS Code Integration

The `.vscode/settings.json` configures the environment for debugging with ASAN.

## � Troubleshooting

### CMake generator mismatch error

```
CMake Error: generator : Ninja
Does not match the generator used previously: Unix Makefiles
```

**Solution:** Clear the FetchContent build cache:
```bash
rm -rf .deps/*-build .deps/*-subbuild
cmake -B build -G Ninja
```

### WebKitGTK not found (Linux)

```
WebKitGTK não encontrado!
```

**Solution:** Install WebKitGTK development package:
```bash
# Ubuntu/Debian
sudo apt install libwebkit2gtk-4.1-dev

# Fedora
sudo dnf install webkit2gtk4.1-devel

# Arch
sudo pacman -S webkit2gtk-4.1
```

### Vite dev server not starting

If the app hangs waiting for Vite:
1. Check if port 5173 is in use: `lsof -i :5173`
2. Try starting Vite manually: `cd ui && npm run dev`
3. Check for npm errors in the UI directory

### ASAN leak reports

WebKitGTK has known memory leaks. Use the wrapper script to suppress them:
```bash
./build/bin/run.sh
```

## �📄 License

MIT License - See [LICENSE](LICENSE) for details.

## 🙏 Credits

- [nikelaz/cpp-webview-gui](https://github.com/nikelaz/cpp-webview-gui) - Original project this was forked from
- [webview/webview](https://github.com/webview/webview) - Cross-platform webview library
- [nlohmann/json](https://github.com/nlohmann/json) - JSON for Modern C++
- [Vue.js](https://vuejs.org/) - Progressive JavaScript Framework
- [Vite](https://vitejs.dev/) - Next Generation Frontend Tooling
