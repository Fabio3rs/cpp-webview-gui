cmake_minimum_required(VERSION 3.16)
project(app LANGUAGES CXX C)

# ==============================================================================
# Configurações de padrão C++
# ==============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# ==============================================================================
# Diretórios de saída
# ==============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
# Otimização: evita re-verificar repos se já foram baixados
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Don't update already fetched content")

# Otimização: cache em diretório fixo para persistir entre rebuilds
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.deps" CACHE PATH "FetchContent cache directory")

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
set(UI_OUTPUT_HEADER "${UI_DIST_DIR}/index_html.h")

# ==============================================================================
# Build do UI (apenas em modo produção)
# ==============================================================================
if(NOT DEV_MODE)
    # Arquivos fonte do UI para detectar alterações
    file(GLOB_RECURSE UI_SOURCES
        "${UI_DIR}/src/*"
        "${UI_DIR}/index.html"
        "${UI_DIR}/vite.config.js"
        "${UI_DIR}/package.json"
        "${UI_DIR}/postbuild.js"
    )

    # Detectar npm
    find_program(NPM_EXECUTABLE npm)
    if(NOT NPM_EXECUTABLE)
        message(FATAL_ERROR "npm não encontrado! Necessário para build de produção.")
    endif()

    # Custom command para gerar o header do UI
    add_custom_command(
        OUTPUT "${UI_OUTPUT_HEADER}"
        COMMAND ${NPM_EXECUTABLE} install --silent
        COMMAND ${NPM_EXECUTABLE} run build
        WORKING_DIRECTORY "${UI_DIR}"
        DEPENDS ${UI_SOURCES}
        COMMENT "Building UI para produção..."
        VERBATIM
    )

    # Custom target para o UI
    add_custom_target(ui DEPENDS "${UI_OUTPUT_HEADER}")
endif()

# ==============================================================================
# Target principal
# ==============================================================================
set(APP_SOURCES
    src/main.cpp
    src/dev_server.h
)

add_executable(app WIN32 ${APP_SOURCES})

# Em modo dev, define macro e não depende do UI build
if(DEV_MODE)
    target_compile_definitions(app PRIVATE
        APP_DEV_MODE=1
        APP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    message(STATUS "Modo DEV: usando Vite dev server (hot reload)")
else()
    # Em produção, depende do build do UI
    add_dependencies(app ui)
    target_include_directories(app PRIVATE "${UI_DIST_DIR}")
    message(STATUS "Modo PROD: UI será embutido no executável")
endif()

# Includes do projeto
target_include_directories(app PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/include"
)

# Includes das dependências como SYSTEM (suprime warnings)
get_target_property(WEBVIEW_INCLUDE_DIRS webview::core INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(JSON_INCLUDE_DIRS nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)

target_include_directories(app SYSTEM PRIVATE
    ${WEBVIEW_INCLUDE_DIRS}
    ${JSON_INCLUDE_DIRS}
)

# Link das bibliotecas
target_link_libraries(app PRIVATE
    webview::core
    nlohmann_json::nlohmann_json
)

# Aplicar flags de warning apenas ao código do projeto
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
