# C++ WebView GUI with Vue 3

C++ desktop application using webview, Vue 3, and typed binary RPC between JavaScript and C++.

## Features

- Cross-platform (Windows, macOS, Linux)
- Hot reload during development
- C++20 with modern features
- CLI options with bash completion
- Sanitizers in debug builds
- Automatic dependency detection

## Binary RPC

Production builds expose an in-process binary endpoint: WebKitGTK and
WKWebView use `app-rpc://native/<method-id>`, while WebView2 intercepts
`https://cpp-webview-gui.invalid/rpc/<method-id>`. The embedded page is loaded
from the same origin, then calls `fetch()` with a `Uint8Array` request body and reads an
`ArrayBuffer` response. All named application bindings use typed
`WireCodec<T>` values end to end; their IDs are derived from their names.
Window settings, results, and errors have fixed wire layouts. Variable
Dockview state and arbitrary event payloads are carried as opaque CBOR bytes:
the native RPC handlers store or forward them without constructing a JSON DOM.
The generated `echoBytes` example carries raw bytes without JSON or base64.
Its `BinaryView` argument borrows the request buffer for the duration of the
synchronous handler; handlers that retain input use owning `Bytes` instead.
The transport accepts
at most 16 MiB per message. Direct typed calls use little-endian integers and
a 32-bit byte length for strings and byte arrays. The C++ and JS codecs also
support optional values and vectors. RPC responses start with `u8 status`:
`0` is followed by the typed result, while `1` is followed by `u32 error_code`
and a length-prefixed UTF-8 error message. HTTP errors are reserved for
transport failures. The page receives the endpoint through
`window.__APP_BINARY_RPC__.endpoint` instead of a hardcoded JS URL.

On Linux, the scheme is registered once per WebKit context. On macOS, the
scheme handler is configured before each WKWebView is constructed. Windows
installs a WebResourceRequested handler in each privileged WebView. Embedded
secondary windows share the dispatcher. Native events use fixed binary tags
for window and drag control messages; arbitrary forwarded JS payloads remain
opaque CBOR. The page fetches each event after a small JavaScript notification
containing only its numeric token. Production pages register binary handlers
directly and install their typed JS functions without registering JSON bindings.

In development, Vite serves the UI and HMR at `127.0.0.1:5173` by default.
Its `/__native_rpc/` proxy forwards byte requests to the native server bound
only to `127.0.0.1:5174` by default; the host injects a fresh token into trusted WebViews.
Named bindings and native events use the same typed binary wire in both modes.
Requests without the token are rejected. Set `APP_VITE_PORT` and `APP_RPC_PORT`
to choose different loopback ports; both Vite and the native host read the same
variables. The ports must differ.
Production startup fails if binary transport cannot be installed;
the event queue does not fall back to textual payloads when full. Real
WebView integration tests exercise binary requests on Linux, Windows, and macOS.
All method IDs, including the byte echo and integer addition examples, are
derived from binding names. The build generates their JavaScript wrappers,
names, and TypeScript declarations
from the C++ binding registry. Dispatcher registration checks ID collisions.
Pages loaded from a caller-supplied `--url` do not receive native bindings,
including in development. A development window is privileged only when it
loads the configured Vite origin. Auxiliary windows receive bindings only when
their initial URL has the trusted origin, or when they load the embedded page.
Privileged WebViews block navigation to other origins before the new document
loads. Production pages on all three platforms now use the internal application
origin. New window actions outside the
application's native window API are blocked. User-clicked external HTTP(S)
links open in the system browser on Linux and macOS. WebView2 exposes a user
gesture flag for new windows, which allows those external HTTP(S) targets to
open in the system browser; external top-level navigation remains blocked.
Script-initiated navigation and other schemes stay blocked. On Linux, the
context-wide binary endpoint also checks that its request comes from an
authorized WebView and rejects an explicitly foreign `Origin`.

`tests/test_binary_rpc_webview.cpp` exercises a real WebKitGTK page with a
15 MiB request and response, typed struct calls, a one-shot binary event, and a
second WebView in the same context. Run it with `ctest --test-dir build` on a machine
with Xvfb. The JS wire tests run with `cd ui && npm run test:binary`.
`tests/test_binary_rpc_platform.cpp` runs the same real `fetch` path on Linux,
Windows, and macOS, including a 15 MiB round trip, an error envelope, and a
typed native event.
For timing comparisons, build `bench_binary_rpc` with sanitizers disabled and
run it under Xvfb. It reports small call latency and 1 MiB JSON/base64 versus
binary round trips; results depend on the installed WebKit and machine.
The legacy bulk baseline echoes base64 without decoding it on the C++ side,
so it favors the old bridge.

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
│       ├── application.h   # Desktop host lifecycle
│       ├── app_bindings.h  # Application RPC entry point
│       ├── handlers.h      # Example methods and handlers
│       ├── native_window_bindings.h # Framework window RPC
│       ├── cli_options.h   # CLI option definitions
│       └── config.h        # App configuration
├── ui/                     # Vue 3 frontend
│   ├── index.html
│   ├── package.json
│   ├── vite.config.js
│   └── src/
│       ├── App.vue         # Small UI entry point
│       ├── demo/DockviewDemo.vue # Full sample UI
│       ├── app_setup.js    # Application Vue setup
│       ├── main.js         # Reusable Vue/native runtime entry
│       ├── native_window_runtime.js # Framework window glue
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
| CMake | ≥ 3.28 |
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

### Where to edit this template

- Add application RPC methods in `src/app/handlers.h` and register them from
  `src/app/app_bindings.h`. `src/app/native_window_bindings.h` contains the
  reusable window API. The build-time emitter calls the same registration
  function without starting a WebView.
- Edit `ui/src/App.vue` for your UI and `ui/src/app_setup.js` for Vue plugins.
  The Dockview sample lives in `ui/src/demo/DockviewDemo.vue`; generated RPC wrappers live in
  `ui/src/generated/`.
- Run `cmake --build build` after changing a C++ binding. The build regenerates
  JavaScript and TypeScript declarations. `cd ui && npm run typecheck` checks
  frontend JS against those declarations; `npm run build` runs that check too.
- For concurrent dev workspaces, launch with matching ports, for example
  `APP_VITE_PORT=5183 APP_RPC_PORT=5184 ./build/bin/app --dev`. The app starts
  Vite with those values. Use the same variables if you start Vite manually.

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

Register a typed method in `src/app/handlers.h` and keep it reachable from
`register_app_bindings()` in `src/app/app_bindings.h`. Primitives and strings
already have wire codecs:

```cpp
APP_BIND_TYPED_WIRE(w, binary, "greet", [](const std::string &name) {
    return std::string("Hello, ") + name;
});
```

Building the C++ project generates `ui/src/generated/native-bindings.js` and
`native-bindings.d.ts`. No JavaScript method list or per-method wrapper needs
editing. Runtime registration and the emitter derive the same 32-bit ID from
the binding name; renaming a binding changes that ID. Collisions fail during
generation and runtime registration. For a simple aggregate DTO, declare its
ordered fields once:

```cpp
struct Greeting { std::string message; };

template <> struct app::binary_rpc::WireFields<Greeting> {
    static constexpr auto fields() {
        return std::make_tuple(
            app::binary_rpc::wire_field("message", &Greeting::message));
    }
};
```

The same field list drives the native codec, generated JS codec, and TypeScript
shape. Supported members include booleans, 32-bit integers, doubles, strings,
bytes, optional values, vectors, and other described structs. Types with a
special wire layout, such as `WindowBootstrap` with opaque Dockview extras,
can supply custom codecs. Both DEV and production use the same binary contract.
For a bulk input that is consumed during the call, use `binary_rpc::BinaryView`;
the generated TypeScript argument remains `Uint8Array`. Copy it into
`binary_rpc::Bytes` before storing it or handing it to asynchronous work.

```javascript
// Call from Vue
const response = await window.greet('Fabio');
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

`APP_BIND_TYPED_WIRE` in `src/app/handlers.h` registers the typed C++ handler.
`emit_native_bindings` generates the JavaScript wrapper during the CMake build;
`ui/src/binary_rpc.js` only handles the transport and wire primitives.

### Dev Server Management

In development mode, the app automatically manages the Vite server:

- Checks the configured Vite port (`APP_VITE_PORT`, default 5173)
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
1. Check if the configured Vite port (default 5173) is in use
2. Try starting Vite manually: `cd ui && npm run dev`
3. Check npm errors

### ASAN leak reports

WebKitGTK has known leaks. Use the wrapper script:
```bash
./build/bin/run.sh
```

## macOS: problema com std::jthread / std::stop_token

Se você está vendo erros como "no type named 'stop_token' in namespace 'std'" ou
"no member named 'jthread' in namespace 'std'" em macOS, é provável que o
clang/stdlib do sistema seja antigo e não ofereça suporte completo ao C++20
usado no projeto. Uma solução simples é instalar uma versão recente do
LLVM/Clang via Homebrew e usar o arquivo de toolchain CMake fornecido.

Passos rápidos:

1. Instale LLVM via Homebrew (Apple Silicon recomenda o prefixo /opt/homebrew):

```bash
brew install llvm
```

2. Configure o CMake para usar o clang/clang++ do Homebrew:

```bash
cmake -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang_toolchain.cmake \
  -DCLANG_BIN_DIR=/opt/homebrew/opt/llvm/bin

cmake --build build
```

Se o Homebrew estiver instalado em outro prefixo (por exemplo `/usr/local`),
ajuste `CLANG_BIN_DIR` para apontar para o diretório `bin` do LLVM.

Observação: o `CMakeLists.txt` agora detecta e avisa se `std::jthread`/
`std::stop_token` não estão disponíveis e recomenda o uso do toolchain acima.

## License

MIT License - See [LICENSE](LICENSE) for details.

## Credits

- [nikelaz/cpp-webview-gui](https://github.com/nikelaz/cpp-webview-gui) - Original project
- [webview/webview](https://github.com/webview/webview) - Webview library
- [nlohmann/json](https://github.com/nlohmann/json) - JSON library
- [Vue.js](https://vuejs.org/) - Frontend framework
- [Vite](https://vitejs.dev/) - Build tool
