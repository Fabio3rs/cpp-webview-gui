cmake_minimum_required(VERSION 3.16)
project(app LANGUAGES CXX C)

# ==============================================================================
# Configurações de padrão C++
# ==============================================================================
# Nota: C++20 é usado porque webview 0.12.0 tem bugs com Clang + C++23
# (incomplete type 'user_script::impl' com std::unique_ptr)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# Diretórios de saída
# ==============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# Configurações do FetchContent (deve vir antes de qualquer FetchContent_Declare)
# ==============================================================================
# Otimização: evita re-verificar repos se já foram baixados
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Don't update already fetched content")

# Otimização: cache em diretório fixo para persistir entre rebuilds
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "FetchContent cache directory")

option(ENABLE_TESTS "Enable tests" ON)

if(ENABLE_TESTS)
    include(CTest)
    enable_testing()

    # Optionally fetch googletest if not available on the system (useful for CI)
    option(FETCH_GTEST "Automatically fetch googletest when not found" ON)

    find_package(GTest QUIET)
    if(GTest_FOUND)
        message(STATUS "GTest found, enabling tests")
        include(GoogleTest)
    else()
        if(FETCH_GTEST)
            message(STATUS "GTest not found, fetching GoogleTest via FetchContent...")
            include(FetchContent)
            FetchContent_Declare(
                googletest
                GIT_REPOSITORY https://github.com/google/googletest.git
                GIT_TAG v1.17.0
                GIT_SHALLOW TRUE
            )
            # Make available will create the targets GTest::gtest and GTest::gtest_main
            FetchContent_MakeAvailable(googletest)

            # prefer the imported targets provided by googletest
            if(TARGET GTest::gtest_main)
                message(STATUS "GoogleTest fetched and available")
                include(GoogleTest)
            else()
                message(WARNING "GoogleTest fetched but expected targets not found; disabling tests.")
                set(ENABLE_TESTS OFF)
            endif()
        else()
            message(WARNING "GTest not found and FETCH_GTEST=OFF; tests will be disabled.")
            set(ENABLE_TESTS OFF)
        endif()
    endif()
endif()

# ==============================================================================
# Opções do projeto (defaults condicionais baseados no build type)
# ==============================================================================
# Detecta se está em modo Debug
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT CMAKE_BUILD_TYPE)
    set(DEFAULT_SANITIZERS ON)
    set(DEFAULT_DEV_MODE ON)
else()
    set(DEFAULT_SANITIZERS OFF)
    set(DEFAULT_DEV_MODE OFF)
endif()

option(ENABLE_SANITIZERS "Enable sanitizers for debugging" ${DEFAULT_SANITIZERS})
option(ENABLE_WARNINGS "Enable compiler warnings" ON)
option(DEV_MODE "Build for development (uses Vite dev server)" ${DEFAULT_DEV_MODE})

# ==============================================================================
# Auto-detecção do WebKitGTK (Linux)
# ==============================================================================
if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)

    set(WEBKITGTK_FOUND FALSE)

    # Tentar WebKitGTK 6.0 (GTK4)
    pkg_check_modules(WEBKITGTK_6 webkitgtk-6.0 QUIET)
    if(WEBKITGTK_6_FOUND)
        set(WEBVIEW_WEBKITGTK_API "6.0")
        set(WEBKITGTK_FOUND TRUE)
        message(STATUS "WebKitGTK 6.0 encontrado (GTK4)")
    endif()

    # Tentar WebKitGTK 4.1 (GTK3)
    if(NOT WEBKITGTK_FOUND)
        pkg_check_modules(WEBKITGTK_4_1 webkit2gtk-4.1 QUIET)
        if(WEBKITGTK_4_1_FOUND)
            set(WEBVIEW_WEBKITGTK_API "4.1")
            set(WEBKITGTK_FOUND TRUE)
            message(STATUS "WebKitGTK 4.1 encontrado (GTK3)")
        endif()
    endif()

    # Tentar WebKitGTK 4.0 (GTK3 legado)
    if(NOT WEBKITGTK_FOUND)
        pkg_check_modules(WEBKITGTK_4_0 webkit2gtk-4.0 QUIET)
        if(WEBKITGTK_4_0_FOUND)
            set(WEBVIEW_WEBKITGTK_API "4.0")
            set(WEBKITGTK_FOUND TRUE)
            message(STATUS "WebKitGTK 4.0 encontrado (GTK3 legado)")
        endif()
    endif()

    if(NOT WEBKITGTK_FOUND)
        message(FATAL_ERROR
            "WebKitGTK não encontrado!\n"
            "Instale uma das versões:\n"
            "  Ubuntu/Debian: sudo apt install libwebkit2gtk-4.1-dev\n"
            "  Fedora: sudo dnf install webkit2gtk4.1-devel\n"
            "  Arch: sudo pacman -S webkit2gtk-4.1"
        )
    endif()

    message(STATUS "Usando WEBVIEW_WEBKITGTK_API = ${WEBVIEW_WEBKITGTK_API}")
endif()

# ==============================================================================
# Flags de warning (serão aplicadas apenas ao código do projeto)
# ==============================================================================
set(PROJECT_WARNING_FLAGS "")

if(ENABLE_WARNINGS)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(STATUS "Configurando flags do G++")
        set(PROJECT_WARNING_FLAGS
            -Wall
            -Wextra
            -Wformat-security
            -Wconversion
            -Wsign-conversion
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(STATUS "Configurando flags do MSVC")
        set(PROJECT_WARNING_FLAGS /W4)
        # Desabilita warnings de funções "inseguras" do CRT (getenv, etc.)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(STATUS "Configurando flags do Clang")
        set(PROJECT_WARNING_FLAGS
            -Wall
            -Wextra
            -Wconversion
            -Wsign-conversion
            -Wno-padded
        )
    endif()
endif()

# ==============================================================================
# Sanitizers (opcional, para debugging)
# ==============================================================================
if(ENABLE_SANITIZERS)
    set(SANITIZER_FLAGS
        -fno-omit-frame-pointer
        -fsanitize=address
        -fsanitize=undefined
        -fsanitize=leak
        -g
    )

    add_compile_options(${SANITIZER_FLAGS})
    add_link_options(${SANITIZER_FLAGS})

    # Copia arquivo de supressões LSAN para o diretório de build
    set(LSAN_SUPPRESSIONS_SRC "${CMAKE_SOURCE_DIR}/.asan_suppressions")
    set(LSAN_SUPPRESSIONS_DST "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/.asan_suppressions")

    if(EXISTS "${LSAN_SUPPRESSIONS_SRC}")
        # Copia o arquivo para o diretório do executável
        configure_file("${LSAN_SUPPRESSIONS_SRC}" "${LSAN_SUPPRESSIONS_DST}" COPYONLY)

        # Cria script wrapper para executar com supressões
        file(WRITE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/run.sh"
"#!/bin/bash
SCRIPT_DIR=\"$(cd \"$(dirname \"\${BASH_SOURCE[0]}\")\" && pwd)\"
export LSAN_OPTIONS=\"suppressions=\${SCRIPT_DIR}/.asan_suppressions\"
exec \"\${SCRIPT_DIR}/app\" \"\$@\"
")
        # Torna o script executável
        file(CHMOD "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/run.sh"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ GROUP_EXECUTE
                        WORLD_READ WORLD_EXECUTE
        )
        message(STATUS "Supressões LSAN: ${LSAN_SUPPRESSIONS_DST}")
        message(STATUS "Use ./build/bin/run.sh para executar sem spam de leaks")
    endif()
endif()

# ==============================================================================
# Dependências externas
# ==============================================================================
# (Configurações de FETCHCONTENT_* foram movidas para o início do arquivo)

# Debug: mostrar progresso do FetchContent (use -DFETCHCONTENT_QUIET=OFF)
# set(FETCHCONTENT_QUIET OFF CACHE BOOL "Show FetchContent progress")

include(FetchContent)

# ------------------------------------------------------------------------------
# nlohmann/json - Tenta usar do sistema, senão baixa
# ------------------------------------------------------------------------------
find_package(nlohmann_json 3.11 QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "nlohmann_json ${nlohmann_json_VERSION} encontrado no sistema")
else()
    message(STATUS "nlohmann_json não encontrado no sistema, baixando...")
    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json
        GIT_TAG v3.12.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(json)
endif()

# ------------------------------------------------------------------------------
# WebView - Sempre baixa (não tem pacote de sistema comum)
# ------------------------------------------------------------------------------
FetchContent_Declare(
    webview
    GIT_REPOSITORY https://github.com/webview/webview
    GIT_TAG 0.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(webview)

# ==============================================================================
# Paths do UI
# ==============================================================================
set(UI_DIR "${CMAKE_SOURCE_DIR}/ui")
set(UI_DIST_DIR "${UI_DIR}/dist")
set(UI_DIST_HTML "${UI_DIST_DIR}/index.html")
set(UI_EMBEDDED_CPP "${CMAKE_BINARY_DIR}/generated/index_html_embedded.cpp")

# ==============================================================================
# Build do UI (apenas em modo produção)
# ==============================================================================
if(NOT DEV_MODE)
    # Arquivos fonte do UI para detectar alterações (exclui dist/)
    file(GLOB UI_SRC_FILES "${UI_DIR}/src/*")
    set(UI_SOURCES
        "${UI_DIR}/index.html"
        "${UI_DIR}/vite.config.js"
        "${UI_DIR}/package.json"
        ${UI_SRC_FILES}
    )

    # Detectar npm
    find_program(NPM_EXECUTABLE npm)
    if(NOT NPM_EXECUTABLE)
        message(FATAL_ERROR "npm não encontrado! Necessário para build de produção.")
    endif()

    # Step 1: Build do UI com Vite (gera dist/index.html)
    add_custom_command(
        OUTPUT "${UI_DIST_HTML}"
        COMMAND ${NPM_EXECUTABLE} install --silent
        COMMAND ${NPM_EXECUTABLE} run build
        WORKING_DIRECTORY "${UI_DIR}"
        DEPENDS ${UI_SOURCES}
        COMMENT "Building UI com Vite..."
        VERBATIM
    )

    # Step 2: Embed do HTML como array C++ (100% CMake, sem Node.js)
    # Usa o script de embedding do diretório cmake/
    set(EMBED_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/EmbedFile.cmake")

    add_custom_command(
        OUTPUT "${UI_EMBEDDED_CPP}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE=${UI_DIST_HTML}
            -DOUTPUT_FILE=${UI_EMBEDDED_CPP}
            -DSYMBOL=index_html
            -P ${EMBED_SCRIPT}
        DEPENDS "${UI_DIST_HTML}" "${EMBED_SCRIPT}"
        COMMENT "Embedding UI como array C++..."
    )

    # Target que agrupa todo o build do UI
    add_custom_target(ui_build DEPENDS "${UI_EMBEDDED_CPP}")
endif()

# ==============================================================================
# Biblioteca do projeto (código testável separado do main)
# ==============================================================================
# Coleta todos os .cpp em src/ exceto main.cpp
file(GLOB_RECURSE LIB_SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*main\\.cpp$")

# Cria a biblioteca estática
add_library(${PROJECT_NAME}_lib STATIC ${LIB_SOURCES})

target_include_directories(${PROJECT_NAME}_lib PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/include"
)

# Includes das dependências como SYSTEM (suprime warnings)
get_target_property(WEBVIEW_INCLUDE_DIRS webview::core INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(JSON_INCLUDE_DIRS nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)

target_include_directories(${PROJECT_NAME}_lib SYSTEM PUBLIC
    ${WEBVIEW_INCLUDE_DIRS}
    ${JSON_INCLUDE_DIRS}
)

# Link das bibliotecas externas
target_link_libraries(${PROJECT_NAME}_lib PUBLIC
    webview::core
    nlohmann_json::nlohmann_json
)

# Aplicar flags de warning
target_compile_options(${PROJECT_NAME}_lib PRIVATE ${PROJECT_WARNING_FLAGS})

# ==============================================================================
# Target principal (executável)
# ==============================================================================
add_executable(app WIN32 src/main.cpp)

# Linka contra a lib do projeto (herda includes e dependências)
target_link_libraries(app PRIVATE ${PROJECT_NAME}_lib)

# Em modo dev, define macro e não depende do UI build
if(DEV_MODE)
    target_compile_definitions(app PRIVATE
        APP_DEV_MODE=1
        APP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    message(STATUS "Modo DEV: usando Vite dev server (hot reload)")
else()
    # Em produção, adiciona o .cpp gerado com o HTML embarcado
    target_sources(app PRIVATE "${UI_EMBEDDED_CPP}")
    add_dependencies(app ui_build)
    message(STATUS "Modo PROD: UI será embutido no executável")
endif()

# Aplicar flags de warning ao executável também
target_compile_options(app PRIVATE ${PROJECT_WARNING_FLAGS})

# ==============================================================================
# Target para rodar em modo dev
# ==============================================================================
add_custom_target(run-dev
    COMMAND ${CMAKE_COMMAND} -E env APP_DEV=1 $<TARGET_FILE:app>
    DEPENDS app
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Executando em modo desenvolvimento..."
    VERBATIM
)

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()

# ==============================================================================
# Informações de build
# ==============================================================================
message(STATUS "")
message(STATUS "=== Configuração do Projeto ===")
message(STATUS "Compilador: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Padrão C++: ${CMAKE_CXX_STANDARD}")
message(STATUS "Modo: ${DEV_MODE} (DEV_MODE)")
message(STATUS "Sanitizers: ${ENABLE_SANITIZERS}")
message(STATUS "Warnings: ${ENABLE_WARNINGS}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
if(UNIX AND NOT APPLE)
    message(STATUS "WebKitGTK API: ${WEBVIEW_WEBKITGTK_API}")
endif()
message(STATUS "===============================")
message(STATUS "")
message(STATUS "Comandos úteis:")
message(STATUS "  cmake --build build                    # Build padrão (prod)")
message(STATUS "  cmake -B build -DDEV_MODE=ON           # Configurar para dev")
message(STATUS "  cmake --build build --target run-dev   # Rodar em modo dev")
message(STATUS "")
