# cmake/clang_toolchain.cmake

# Try to obtain CLANG_BIN_DIR from (in order):
#  1) CMake variable passed via -DCLANG_BIN_DIR=...
#  2) environment variable CLANG_BIN_DIR
#  3) brew --prefix llvm (or llvm@20, llvm@19, ...)
if(NOT DEFINED CLANG_BIN_DIR)
  if(DEFINED ENV{CLANG_BIN_DIR} AND NOT "" STREQUAL "$ENV{CLANG_BIN_DIR}")
    set(CLANG_BIN_DIR $ENV{CLANG_BIN_DIR})
    message(STATUS "CLANG_BIN_DIR set from environment: ${CLANG_BIN_DIR}")
  else()
    # Try common Homebrew formula names
    set(_brew_candidates llvm llvm@20 llvm@19 llvm@18)
    set(_found_prefix "")
    foreach(_name IN LISTS _brew_candidates)
      execute_process(COMMAND brew --prefix ${_name}
                      RESULT_VARIABLE _ret
                      OUTPUT_VARIABLE _out
                      ERROR_QUIET
                      OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(_ret EQUAL 0 AND NOT "${_out}" STREQUAL "")
        set(_found_prefix "${_out}")
        break()
      endif()
    endforeach()

    if(NOT "${_found_prefix}" STREQUAL "")
      set(CLANG_BIN_DIR "${_found_prefix}/bin")
      message(STATUS "CLANG_BIN_DIR auto-detected via brew: ${CLANG_BIN_DIR}")
    endif()
  endif()
endif()

if(NOT DEFINED CLANG_BIN_DIR OR "${CLANG_BIN_DIR}" STREQUAL "")
  message(FATAL_ERROR "CLANG_BIN_DIR não definido (passe -DCLANG_BIN_DIR=/opt/homebrew/opt/llvm@20/bin)\n"
          "Ou exporte CLANG_BIN_DIR no ambiente ou instale LLVM via Homebrew (brew install llvm).")
endif()

# Prefer explicit binaries from CLANG_BIN_DIR, but fall back to finding clang/clang++ on PATH
if(EXISTS "${CLANG_BIN_DIR}/clang")
  set(_cc "${CLANG_BIN_DIR}/clang")
else()
  find_program(_cc clang)
endif()

if(EXISTS "${CLANG_BIN_DIR}/clang++")
  set(_cxx "${CLANG_BIN_DIR}/clang++")
else()
  find_program(_cxx clang++)
endif()

if(DEFINED _cc AND NOT "${_cc}" STREQUAL "")
  set(CMAKE_C_COMPILER "${_cc}" CACHE STRING "" FORCE)
else()
  message(STATUS "Não foi possível determinar clang; deixarei CMake escolher o compilador padrão")
endif()

if(DEFINED _cxx AND NOT "${_cxx}" STREQUAL "")
  set(CMAKE_CXX_COMPILER "${_cxx}" CACHE STRING "" FORCE)
else()
  message(STATUS "Não foi possível determinar clang++; deixarei CMake escolher o compilador padrão")
endif()

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
