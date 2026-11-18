cmake_minimum_required(VERSION 3.20)
project(CppCompiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Solo main.cpp es unidad de compilacion; el resto son encabezados (.h)
add_executable(CppCompiler main.cpp)
if(MSVC)
    set(CPP_NATIVE_MSVC 1)
endif()
configure_file(NativeCompilerConfig.h.in NativeCompilerConfig.h @ONLY)
target_include_directories(CppCompiler PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})

# Advertencias estrictas
if(MSVC)
    target_compile_options(CppCompiler PRIVATE /W4)
else()
    target_compile_options(CppCompiler PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Sanitizers: cualquier comportamiento indefinido / error de memoria aborta el programa.
# (Desactivalo con -DSTRICT_SANITIZE=OFF; en Windows no se activan.)
option(STRICT_SANITIZE "Abortar ante UB y errores de memoria" ON)
if(STRICT_SANITIZE AND NOT WIN32)
    target_compile_options(CppCompiler PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(CppCompiler PRIVATE -fsanitize=address,undefined)
endif()

