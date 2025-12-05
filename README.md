# C++ WebView GUI with Vue 3

C++ desktop application using webview for cross-platform GUI, Vue 3 for the frontend, and nlohmann/json for JSON parsing.

## Features

- Cross-platform (Windows, macOS, Linux)
- Hot reload during development
- C++20 with modern features
- CLI options with bash completion
- Sanitizers in debug builds
- Automatic dependency detection

## Project Structure

```
├── CMakeLists.txt          # Build configuration
├── cmake/
│   └── EmbedFile.cmake     # Embeds UI as C++ byte array
├── include/
│   ├── option_parser.hpp   # CLI argument parser
│   ├── option_parser_decls.hpp
│   ├── option_parser_impl.hpp
│   ├── expected.hpp        # C++20 std::expected wrapper
│   └── embedded_resources.h # Embedded UI interface
├── src/
│   ├── main.cpp            # Entry point
│   ├── lib.cpp             # Library code
│   ├── dev_server.h       # Vite dev server management
│   └── app/
│       ├── application.h   # Main application class
│       ├── bindings.h      # JS ↔ C++ bindings
│       ├── cli_options.h   # CLI option definitions
│       └── config.h        # App configuration
├── ui/                     # Vue 3 frontend
│   ├── index.html
│   ├── package.json
│   ├── vite.config.js
│   └── src/
│       ├── App.vue         # Main Vue component
│       ├── main.js         # Vue app entry
│       └── style.css       # Global styles
├── tests/                  # GoogleTest unit tests
│   ├── CMakeLists.txt
│   └── test.cpp
└── .vscode/
    └── settings.json       # VS Code integration
```

## Dependencies

### Required

| Tool | Version |
|------|---------|
| CMake | ≥ 3.16 |
| C++ Compiler | C++20 support |
| Node.js | Latest LTS |
| Ninja | Recommended |

### Platform-specific

| Platform | Requirements |
|----------|-------------|
| Linux | WebKitGTK 4.1+ |
| macOS | WebKit (included) |
| Windows | WebView2 Runtime |

### Auto-detected Libraries

The build system detects and uses system libraries when available:

- nlohmann/json - Falls back to FetchContent if not installed
- WebKitGTK - Auto-detects version (6.0 → 4.1 → 4.0)

## Quick Start

### Development Mode

```bash
# Install UI dependencies (first time only)
cd ui && npm install && cd ..

# Configure for development
cmake -B build -G Ninja -DDEV_MODE=ON

# Build
cmake --build build

# Run
./build/bin/app --dev
```

The app automatically starts the Vite dev server if not running. Changes in `ui/src/` are reflected immediately.

### Production Build

```bash
# Configure for production
cmake -B build -G Ninja -DDEV_MODE=OFF

# Build (automatically builds UI)
cmake --build build

# Run
./build/bin/app
```

CMake automatically builds the UI and embeds it into the executable.

## CLI Options

```
Usage: app [OPTIONS]

Options:
  -d, --dev                   Force development mode
  -p, --prod                  Force production mode
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

The app supports bash completion:

```bash
COMP_LINE="app --" COMP_POINT=7 ./build/bin/app
# Output: --dev --prod --verbose --version --width --height --url
```

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| DEV_MODE | ON (Debug) | Use Vite dev server instead of embedded HTML |
| ENABLE_TESTS | ON | Enable unit tests with GoogleTest |
| FETCH_GTEST | ON | Download GoogleTest if not found |
| ENABLE_SANITIZERS | ON (Debug) | Enable ASAN/UBSAN/LSAN |
| ENABLE_WARNINGS | ON | Enable compiler warnings |
| FETCHCONTENT_QUIET | ON | Show FetchContent download progress |

```bash
# Production build without sanitizers
cmake -B build -DDEV_MODE=OFF -DENABLE_SANITIZERS=OFF -DCMAKE_BUILD_TYPE=Release

# Verbose dependency download
cmake -B build -DFETCHCONTENT_QUIET=OFF
```

### Dependency Caching

FetchContent dependencies are cached in `.deps/`:

- Persistent cache survives `rm -rf build`
- Shallow clones for faster downloads
- System library detection

To force re-download:
```bash
rm -rf .deps
cmake -B build
```

### UI Embedding

Production builds embed the HTML directly into the executable using CMake.

How it works:
1. Vite builds the UI into `dist/index.html`
2. CMake converts the HTML to a C++ byte array
3. Generated code is compiled into the executable
4. Application uses `embedded::index_html_str()`

Benefits:
- Portable across compilers/platforms
- No MSVC string literal limits
- Automatic rebuild on UI changes
- No runtime dependencies

## Customization

### Adding JS ↔ C++ Bindings

Bindings are defined in `src/app/bindings.h`:

```cpp
// Add binding
webview.bind("my_function", [](const std::string& request) -> std::string {
    auto json = nlohmann::json::parse(request);
    // Process request
    return nlohmann::json{{"result", "ok"}}.dump();
});
```

```javascript
// Call from Vue
const response = await window.my_function({data: "hello"});
```

### Modifying the Vue UI

The frontend uses Vue 3 + Vite. The app manages the dev server automatically:

```bash
./build/bin/app --dev
```

File structure:
```
ui/src/
├── App.vue          # Main component
├── main.js          # Vue setup
└── style.css        # Global styles
```

Changes in `ui/src/` are reflected immediately without rebuilding.

### Integrating C++ Libraries

Add dependencies using FetchContent:

```cmake
# In CMakeLists.txt
include(FetchContent)

FetchContent_Declare(
    my_library
    GIT_REPOSITORY https://github.com/user/my_library.git
    GIT_TAG v1.0.0
)

FetchContent_MakeAvailable(my_library)

target_link_libraries(${PROJECT_NAME}_lib PRIVATE my_library)
```

For system libraries:
```cmake
find_package(SomeLib QUIET)
if(SomeLib_FOUND)
    target_link_libraries(${PROJECT_NAME}_lib PRIVATE SomeLib::SomeLib)
else()
    # Fallback to FetchContent
endif()
```

## Architecture

### JS ↔ C++ Communication

Frontend calls C++ via `window.ping()`:

```javascript
window.ping(message)
```

```cpp
// In bindings.h
webview.bind("ping", [](const std::string& req) {
    return response;
});
```

### Dev Server Management

In development mode, the app automatically manages the Vite server:

- Checks if server is running on port 5173
- Starts `npm run dev` if not running
- Waits for server to be ready
- Terminates server on exit if it started it

### Option Parser

Custom CLI parser with type-safe option definitions and bash completion.

## Debugging

### Sanitizers

Debug builds include Address Sanitizer. To suppress known leaks:

```bash
# Using wrapper script
./build/bin/run.sh

# Or manually
LSAN_OPTIONS="suppressions=.asan_suppressions" ./build/bin/app
```

### VS Code Integration

`.vscode/settings.json` configures debugging with ASAN.

## Troubleshooting

### CMake generator mismatch

```
CMake Error: generator : Ninja
Does not match the generator used previously: Unix Makefiles
```

Solution:
```bash
rm -rf .deps/*-build .deps/*-subbuild
cmake -B build -G Ninja
```

### WebKitGTK not found (Linux)

Install WebKitGTK:
```bash
# Ubuntu/Debian
sudo apt install libwebkit2gtk-4.1-dev

# Fedora
sudo dnf install webkit2gtk4.1-devel

# Arch
sudo pacman -S webkit2gtk-4.1
```

### Vite dev server not starting

If the app hangs:
1. Check if port 5173 is in use
2. Try starting Vite manually: `cd ui && npm run dev`
3. Check npm errors

### ASAN leak reports

WebKitGTK has known leaks. Use the wrapper script:
```bash
./build/bin/run.sh
```

## License

MIT License - See [LICENSE](LICENSE) for details.

## Credits

- [nikelaz/cpp-webview-gui](https://github.com/nikelaz/cpp-webview-gui) - Original project
- [webview/webview](https://github.com/webview/webview) - Webview library
- [nlohmann/json](https://github.com/nlohmann/json) - JSON library
- [Vue.js](https://vuejs.org/) - Frontend framework
- [Vite](https://vitejs.dev/) - Build tool
