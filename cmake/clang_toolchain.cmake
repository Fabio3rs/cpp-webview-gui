# cmake/clang_toolchain.cmake

if(NOT DEFINED CLANG_BIN_DIR)
  message(FATAL_ERROR "CLANG_BIN_DIR não definido (passe -DCLANG_BIN_DIR=/opt/homebrew/opt/llvm@20/bin)")
endif()

set(CMAKE_C_COMPILER "${CLANG_BIN_DIR}/clang" CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER "${CLANG_BIN_DIR}/clang++" CACHE STRING "" FORCE)

# Força C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
  # Descobre o prefixo do LLVM (…/opt/llvm@20)
  get_filename_component(LLVM_PREFIX "${CLANG_BIN_DIR}/.." ABSOLUTE)

  set(LLVM_LIBCXX_DIR    "${LLVM_PREFIX}/lib/c++")
  set(LLVM_LIBUNWIND_DIR "${LLVM_PREFIX}/lib/unwind")

  # Usa a libc++ do LLVM para linkar
  set(CMAKE_EXE_LINKER_FLAGS
      "${CMAKE_EXE_LINKER_FLAGS} -L${LLVM_LIBCXX_DIR} -L${LLVM_LIBUNWIND_DIR} -lunwind")
  set(CMAKE_SHARED_LINKER_FLAGS
      "${CMAKE_SHARED_LINKER_FLAGS} -L${LLVM_LIBCXX_DIR} -L${LLVM_LIBUNWIND_DIR} -lunwind")

  # **Importante**: você provavelmente não precisa mais de -fexperimental-library
  # em LLVM 20+, então se em algum lugar você faz:
  #   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexperimental-library")
  # vale condicionar isso a AppleClang antigo ou simplesmente remover.
endif()
