# Toolchain helper para usar um clang/llvm instalado (ex: Homebrew) no macOS
# Uso: cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/clang_toolchain.cmake -DCLANG_BIN_DIR=/opt/homebrew/opt/llvm/bin

if(NOT DEFINED CLANG_BIN_DIR)
    message(STATUS "No CLANG_BIN_DIR provided. Toolchain will not override compiler.")
    return()
endif()

set(CLANG_BIN_DIR "${CLANG_BIN_DIR}" CACHE PATH "Diretório contendo clang/clang++/llvm tools")

# Prefer full paths to binários
find_program(CLANG_PATH clang HINTS ${CLANG_BIN_DIR})
find_program(CLANGXX_PATH clang++ HINTS ${CLANG_BIN_DIR})
find_program(LLVM_AR_PATH llvm-ar HINTS ${CLANG_BIN_DIR})

if(NOT CLANGXX_PATH)
    message(FATAL_ERROR "clang++ não encontrado em: ${CLANG_BIN_DIR}")
endif()

message(STATUS "Forçando uso do clang: ${CLANGXX_PATH}")

set(CMAKE_C_COMPILER "${CLANG_PATH}")
set(CMAKE_CXX_COMPILER "${CLANGXX_PATH}")

# Se llvm-ar estiver disponível, informe ao CMake para usá-lo como AR
if(LLVM_AR_PATH)
    set(CMAKE_AR "${LLVM_AR_PATH}")
    message(STATUS "Usando llvm-ar: ${LLVM_AR_PATH}")
endif()

# Ajustes de flags úteis para Homebrew LLVM em macOS Apple Silicon
if(APPLE)
    # Evita linking contra a toolchain do sistema inadvertidamente
    set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-search_paths_first")
endif()
