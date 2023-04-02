cmake_minimum_required(VERSION 3.17)
project(interpolate)

## Find LLVM & Clang
set(CMAKE_FIND_PACKAGE_RESOLVE_SYMLINKS TRUE)
find_package(LLVM 13 REQUIRED CONFIG
  PATHS "/usr/local/opt/llvm@13"
)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake from ${LLVM_DIR}")

find_package(Clang REQUIRED CONFIG
  HINTS "${LLVM_INSTALL_PREFIX}/lib/cmake/clang"
)
message(STATUS "Found Clang from ${CLANG_CMAKE_DIR}")

add_definitions(${LLVM_DEFINITIONS})
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
include_directories(SYSTEM ${CLANG_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 \
-Wredundant-decls -Wcast-align -Wmissing-include-dirs -Wswitch-default \
-Wextra -Wall -Winvalid-pch -Wredundant-decls -Wformat=2 \
-Wmissing-format-attribute -Wformat-nonliteral -Werror")

execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/llvm-config --libs all OUTPUT_VARIABLE LLVM_LIB)
string(STRIP ${LLVM_LIB} LLVM_LIB)

find_program(CLANG_BINARY "clang"
  HINTS ${LLVM_TOOLS_BINARY_DIR}
  DOC "The clang binary to use in the symcc wrapper script.")
find_program(CLANGPP_BINARY "clang++"
  HINTS ${LLVM_TOOLS_BINARY_DIR}
  DOC "The clang binary to use in the sym++ wrapper script.")
if (NOT CLANG_BINARY)
  message(FATAL_ERROR "Clang not found; please make sure that the version corresponding to your LLVM installation is available.")
endif()

if (${LLVM_VERSION_MAJOR} LESS 13)
  set(CLANG_LOAD_PASS "-Xclang -load -Xclang ")
else()
  set(CLANG_LOAD_PASS "-fpass-plugin=")
endif()

configure_file("pass/cc.in" "cc" @ONLY)

add_subdirectory(pass)
add_subdirectory(tests)
