#pragma once
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "NativeCompilerConfig.h"

namespace native_backend {
namespace fs = std::filesystem;

inline std::string shellQuote(const std::string& value) {
#ifdef _WIN32
    // Quoting compatible con CreateProcess/cmd para rutas y argumentos usuales.
    std::string out = "\"";
    size_t slashes = 0;
    for (char ch : value) {
        if (ch == '\\') { ++slashes; continue; }
        if (ch == '"') {
            out.append(slashes * 2 + 1, '\\');
            out += '"';
            slashes = 0;
        } else {
            out.append(slashes, '\\');
            slashes = 0;
            out += ch;
        }
    }
    out.append(slashes * 2, '\\');
    out += '"';
    return out;
#else
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') out += "'\\''";
        else out += ch;
    }
    out += '\'';
    return out;
#endif
}

inline std::string defaultObjectPath(const std::string& source) {
    fs::path path(source);
#ifdef _WIN32
    path.replace_extension(".obj");
#else
    path.replace_extension(".o");
#endif
    return path.string();
}

inline int compileObject(const std::string& source,
                         const std::string& output,
                         std::ostream& log) {
    if (!fs::exists(source) || !fs::is_regular_file(source))
        throw std::runtime_error("No existe el archivo fuente: " + source);
    if (output.empty()) throw std::runtime_error("La ruta del objeto nativo esta vacia.");

    const fs::path outPath(output);
    if (outPath.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(outPath.parent_path(), ec);
        if (ec) throw std::runtime_error("No se pudo crear el directorio de salida: " + ec.message());
    }

    const std::string compiler = CPP_NATIVE_COMPILER;
#ifdef CPP_NATIVE_MSVC
    const std::string command = shellQuote(compiler) + " /nologo /std:c++17 /W4 /c " +
        shellQuote(source) + " /Fo" + shellQuote(output);
#else
    const std::string command = shellQuote(compiler) + " -std=c++17 -Wall -Wextra -pedantic -c " +
        shellQuote(source) + " -o " + shellQuote(output);
#endif

    log << "Backend nativo C++17\n"
        << "  Compilador: " << compiler << "\n"
        << "  Entrada: " << source << "\n"
        << "  Objeto: " << output << "\n";
    const int status = std::system(command.c_str());
    if (status != 0) {
        std::error_code ec;
        fs::remove(outPath, ec); // nunca dejar un objeto parcial como si fuera valido
        log << "  El compilador anfitrion rechazo el programa (codigo " << status << ").\n";
        return status == -1 ? 127 : 1;
    }
    if (!fs::exists(outPath) || fs::file_size(outPath) == 0)
        throw std::runtime_error("El compilador anfitrion no produjo un objeto nativo valido.");
    log << "  Objeto nativo generado correctamente (" << fs::file_size(outPath) << " bytes).\n";
    return 0;
}
} // namespace native_backend
