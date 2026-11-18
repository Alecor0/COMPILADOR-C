#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cctype>
#include <regex>
#include <utility>
#include <algorithm>
#include <cstdio>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "Scanner.h"
#include "Parser.h"
#include "SymbolTable.h"
#include "Semantic.h"
#include "IntermediateCode.h"
#include "IROptimizer.h"
#include "ObjectCodeGenerator.h"
#include "VirtualMachine.h"
#include "NativeBackend.h"

namespace fs = std::filesystem;

// Presentacion de las fases integrada directamente en main.cpp.
// No existe una dependencia separada de Report.h.
static string tokenTypeLabel(TokenType type) {
    switch (type) {
        case KEYWORD:     return "PALABRA RESERVADA C++";
        case IDENTIFIER:  return "IDENTIFICADOR";
        case NUMBER:      return "NUMERO ENTERO";
        case DOUBLE_LIT:  return "NUMERO DECIMAL";
        case STRING_LIT:  return "LITERAL DE CADENA/CARACTER";
        case OPERATOR:    return "OPERADOR";
        case SYMBOL:      return "SIMBOLO";
        case END_OF_FILE: return "FIN DE ARCHIVO";
        default:          return "TOKEN DESCONOCIDO";
    }
}

static string escapeDisplayText(const string& value) {
    string escaped;
    for (char ch : value) {
        switch (ch) {
            case '\n': escaped += "\\n"; break;
            case '\t': escaped += "\\t"; break;
            case '\r': escaped += "\\r"; break;
            default: escaped += ch; break;
        }
    }
    return escaped;
}

static string padRight(const string& value, size_t width) {
    return value.size() >= width ? value : value + string(width - value.size(), ' ');
}

static void printTokenDetails(const vector<Token>& tokens, std::ostream& out) {
    out << "  " << padRight("#", 5) << padRight("LINEA", 7)
        << padRight("CATEGORIA", 25) << "LEXEMA C++\n";
    out << "  " << string(58, '-') << "\n";

    int count = 0;
    for (const Token& token : tokens) {
        if (token.type == END_OF_FILE) continue;
        out << "  " << padRight(to_string(++count), 5)
            << padRight(to_string(token.line), 7)
            << padRight(tokenTypeLabel(token.type), 25)
            << "'" << escapeDisplayText(token.value) << "'\n";
    }

    const TokenType categories[] = {
        KEYWORD, IDENTIFIER, NUMBER, DOUBLE_LIT, STRING_LIT, OPERATOR, SYMBOL
    };
    out << "\n  RESUMEN DEL SCANNER (" << count << " tokens)\n";
    for (TokenType category : categories) {
        vector<std::pair<string, int>> frequencies;
        int categoryCount = 0;
        for (const Token& token : tokens) {
            if (token.type != category) continue;
            ++categoryCount;
            auto found = std::find_if(frequencies.begin(), frequencies.end(),
                [&](const std::pair<string, int>& item) { return item.first == token.value; });
            if (found == frequencies.end()) frequencies.push_back({token.value, 1});
            else ++found->second;
        }
        if (categoryCount == 0) continue;
        out << "  - " << tokenTypeLabel(category) << " (" << categoryCount << "): ";
        for (size_t i = 0; i < frequencies.size(); ++i) {
            if (i) out << ", ";
            out << escapeDisplayText(frequencies[i].first) << " x" << frequencies[i].second;
        }
        out << "\n";
    }
}

static string describeAstNode(const NP& node) {
    string description = nkName(node->k);
    if (!node->name.empty()) description += "  nombre=" + node->name;
    if (!node->type.empty()) description += "  tipo=" + node->type;
    if (!node->op.empty() && node->k != NK::Method) description += "  operador=" + node->op;
    if (node->k == NK::Method) description += "  clase=" + node->op;
    if (node->k == NK::Lit) {
        const string literal = node->val.t == Value::STR
            ? "\"" + escapeDisplayText(node->val.s) + "\"" : toStr(node->val);
        description += "  valor=" + literal;
    }
    if (node->k == NK::Block && node->flag) description += "  (bloque sin ambito propio)";
    if (node->k == NK::IncDec) description += node->flag ? "  (prefijo)" : "  (postfijo)";
    description += "   [linea " + to_string(node->line) + "]";
    return description;
}

static const vector<string>& astChildLabels(NK kind) {
    static const vector<string> none;
    static const vector<string> forLabels = {"inicializacion", "condicion", "actualizacion", "cuerpo"};
    static const vector<string> ifLabels = {"condicion", "entonces", "si-no"};
    static const vector<string> whileLabels = {"condicion", "cuerpo"};
    static const vector<string> doLabels = {"cuerpo", "condicion"};
    static const vector<string> binaryLabels = {"izquierda", "derecha"};
    static const vector<string> assignLabels = {"destino", "valor"};
    static const vector<string> ternaryLabels = {"condicion", "si-verdadero", "si-falso"};
    static const vector<string> methodCallLabels = {"objeto", "argumento 1", "argumento 2", "argumento 3", "argumento 4"};
    static const vector<string> switchLabels = {"selector"};
    static const vector<string> caseLabels = {"valor"};

    switch (kind) {
        case NK::For: return forLabels;
        case NK::If: return ifLabels;
        case NK::While: return whileLabels;
        case NK::DoWhile: return doLabels;
        case NK::Binary: return binaryLabels;
        case NK::Assign: return assignLabels;
        case NK::Ternary: return ternaryLabels;
        case NK::MethodCall: return methodCallLabels;
        case NK::Switch: return switchLabels;
        case NK::Case: return caseLabels;
        default: return none;
    }
}

static void printAstTree(const NP& node, std::ostream& out,
                         const string& label = "", int depth = 0) {
    const string indent(2 + static_cast<size_t>(depth) * 3, ' ');
    const string prefix = label.empty() ? "" : label + ": ";
    if (!node) {
        out << indent << "+- " << prefix << "(vacio)\n";
        return;
    }

    out << indent << "+- " << prefix << describeAstNode(node) << "\n";
    const vector<string>& labels = astChildLabels(node->k);
    for (size_t i = 0; i < node->b.size(); ++i) {
        string childLabel = "b";
        if (node->k == NK::Method) childLabel = "parametro";
        else if (node->k == NK::Try) childLabel = "captura";
        else if (node->k == NK::Switch) childLabel = "seccion";
        printAstTree(node->b[i], out, childLabel, depth + 1);
    }
    for (size_t i = 0; i < node->a.size(); ++i) {
        printAstTree(node->a[i], out, i < labels.size() ? labels[i] : "", depth + 1);
    }
}

static int countAstNodes(const NP& node) {
    if (!node) return 0;
    int count = 1;
    for (const NP& child : node->a) count += countAstNodes(child);
    for (const NP& child : node->b) count += countAstNodes(child);
    return count;
}

static void banner(std::ostream& out, const std::string& title) {
    out << "\n==================================================================\n"
        << title << "\n"
        << "==================================================================\n";
}

static void writeTextFile(const string& path, const string& content) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("No se pudo crear el archivo: " + path);
    file << content;
    if (!file) throw std::runtime_error("Fallo al escribir el archivo: " + path);
}

static string usage() {
    return "Uso: CppCompiler [opciones] [archivo.cpp | nombre]\n"
           "  Sin archivo, toma los .cpp de la carpeta 'codigos' (si hay varios, pregunta cual).\n"
           "  Un nombre sin ruta se busca primero en el directorio actual y luego en 'codigos'.\n"
           "Opciones:\n"
           "  --simple         muestra solo la salida del programa\n"
           "  --explained      muestra las seis fases (tokens, AST, TAC, optimizacion, binario) y la ejecucion\n"
           "  --todos          procesa todos los .cpp de la carpeta 'codigos'\n"
           "  --listar         lista los archivos de la carpeta 'codigos'\n"
           "  --native         genera un objeto nativo con el compilador del sistema (-o salida.o)\n"
           "  -o ruta          prefijo de los archivos generados (o ruta del objeto con --native)\n"
           "Sin --simple ni --explained: en una terminal pregunta el tipo de salida; si la entrada\n"
           "no es una terminal (scripts, pruebas) se usa la salida simple.\n"
           "El modo explicado guarda .ir, .opt.ir, .cobj y .obj.txt en la carpeta 'salida'.";
}

static bool hasInteractiveInput() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

static string trimLower(string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Devuelve true para salida explicada.
static bool askOutputMode() {
    std::string answer;
    for (;;) {
        std::cerr << "\nSeleccione el tipo de salida:\n"
                  << "  1) Salida simple\n"
                  << "  2) Salida explicada\n"
                  << "Opcion [1/2, predeterminada 2]: " << std::flush;
        if (!std::getline(std::cin, answer)) { std::cerr << "\n"; return true; }
        answer = trimLower(answer);
        if (answer.empty() || answer == "2" || answer == "e" || answer == "explicada") return true;
        if (answer == "1" || answer == "s" || answer == "simple") return false;
        std::cerr << "Opcion no valida. Escriba 1 o 2.\n";
    }
}

static bool isCppTranslationUnit(const string& path) {
    string ext = fs::path(path).extension().string();
    for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++";
}

// ------------------------------------------------------------ carpeta 'codigos'
static fs::path findCodesDir(const char* argv0) {
    std::error_code ec;
    vector<fs::path> candidates = {fs::path("codigos")};
    fs::path exe = fs::absolute(fs::path(argv0), ec);
    if (!ec && exe.has_parent_path()) {
        candidates.push_back(exe.parent_path() / "codigos");
        candidates.push_back(exe.parent_path().parent_path() / "codigos");
    }
    for (const auto& c : candidates)
        if (fs::is_directory(c, ec)) return c;
    return fs::path();
}

static vector<string> listSources(const fs::path& dir) {
    vector<string> files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec))
        if (entry.is_regular_file(ec) && isCppTranslationUnit(entry.path().string())) files.push_back(entry.path().string());
    std::sort(files.begin(), files.end());
    return files;
}

static string resolveInput(const string& name, const fs::path& codesDir) {
    std::error_code ec;
    if (fs::is_regular_file(name, ec)) return name;
    if (!codesDir.empty()) {
        for (const string& candidate : {name, name + ".cpp"}) {
            fs::path p = codesDir / candidate;
            if (fs::is_regular_file(p, ec)) return p.string();
        }
    }
    return name;   // se reportara como inexistente al abrirlo
}

static string chooseFile(const vector<string>& files) {
    std::cerr << "\nArchivos en la carpeta de codigos:\n";
    for (size_t k = 0; k < files.size(); ++k)
        std::cerr << "  " << (k + 1) << ") " << fs::path(files[k]).filename().string() << "\n";
    std::string answer;
    for (;;) {
        std::cerr << "Elija un archivo [1-" << files.size() << "]: " << std::flush;
        if (!std::getline(std::cin, answer)) return "";
        answer = trimLower(answer);
        try {
            size_t used = 0;
            const int n = std::stoi(answer, &used);
            if (used == answer.size() && n >= 1 && n <= static_cast<int>(files.size())) return files[static_cast<size_t>(n - 1)];
        } catch (...) {}
        for (const auto& f : files)
            if (trimLower(fs::path(f).filename().string()) == answer || trimLower(fs::path(f).stem().string()) == answer) return f;
        std::cerr << "Opcion no valida.\n";
    }
}

// ------------------------------------------------------------ deteccion de otros lenguajes
static string removeCommentsAndLiterals(const string& source) {
    string out = source;
    enum class State { Code, LineComment, BlockComment, StringLiteral, CharLiteral };
    State state = State::Code;
    bool escaped = false;
    for (size_t i = 0; i < out.size(); ++i) {
        const char c = source[i];
        const char next = (i + 1 < source.size()) ? source[i + 1] : '\0';
        if (state == State::Code) {
            if (c == '/' && next == '/') { out[i] = out[i + 1] = ' '; ++i; state = State::LineComment; }
            else if (c == '/' && next == '*') { out[i] = out[i + 1] = ' '; ++i; state = State::BlockComment; }
            else if (c == '"') { out[i] = ' '; state = State::StringLiteral; escaped = false; }
            else if (c == '\'') { out[i] = ' '; state = State::CharLiteral; escaped = false; }
        } else if (state == State::LineComment) {
            if (c == '\n') state = State::Code; else out[i] = ' ';
        } else if (state == State::BlockComment) {
            out[i] = ' ';
            if (c == '*' && next == '/') { out[i + 1] = ' '; ++i; state = State::Code; }
        } else {
            out[i] = (c == '\n') ? '\n' : ' ';
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if ((state == State::StringLiteral && c == '"') ||
                     (state == State::CharLiteral && c == '\'')) state = State::Code;
        }
    }
    return out;
}

static bool looksLikeNonCppSyntax(const string& source) {
    const string code = removeCommentsAndLiterals(source);
    const std::regex patterns[] = {
        std::regex(R"(\bSystem\s*\.\s*out\s*\.)"),
        std::regex(R"(\bpublic\s+class\b)"),
        std::regex(R"(\bpublic\s+static\s+void\s+main\b)"),
        std::regex(R"(\bString\s*(?:\[\s*\]|[A-Za-z_]))"),
        std::regex(R"(\bboolean\s+[A-Za-z_])"),
        std::regex(R"(\bprintln\s*\()")
    };
    for (const auto& pattern : patterns) if (std::regex_search(code, pattern)) return true;
    return false;
}

// ------------------------------------------------------------ resumen semantico (modo explicado)
static void collectSymbols(const NP& n, const string& fn, vector<std::tuple<string, string, string, bool>>& out) {
    if (!n) return;
    if (n->k == NK::VarDecl) out.emplace_back(fn, n->name, n->type, n->flag);
    for (const NP& c : n->a) collectSymbols(c, fn, out);
    for (const NP& c : n->b) collectSymbols(c, fn, out);
}

static void printSemanticSummary(const NP& ast, std::ostream& out) {
    int functions = 0;
    vector<std::tuple<string, string, string, bool>> symbols;
    for (const NP& unit : ast->a) {
        for (const NP& m : unit->a) {
            ++functions;
            out << "  funcion " << m->type << " " << m->name << "(";
            for (size_t k = 0; k < m->b.size(); ++k) out << (k ? ", " : "") << m->b[k]->type << " " << m->b[k]->name;
            out << ")\n";
            collectSymbols(m, m->name, symbols);
        }
    }
    out << "\n  TABLA DE SIMBOLOS\n  " << padRight("FUNCION", 20) << padRight("VARIABLE", 18) << "TIPO\n  " << string(52, '-') << "\n";
    for (const auto& s : symbols)
        out << "  " << padRight(std::get<0>(s), 20) << padRight(std::get<1>(s), 18)
            << (std::get<3>(s) ? "const " : "") << std::get<2>(s) << "\n";
    out << "\n  Analisis semantico terminado: " << functions << " funcion(es), " << symbols.size()
        << " variable(s)/parametro(s), 0 errores.\n";
}

// ------------------------------------------------------------ pipeline completo para un archivo
static int compileFile(const string& inputPath, bool explained, const string& outputPrefixArg) {
    std::ifstream input(inputPath, std::ios::binary);
    if (!input) {
        std::cerr << "[ERROR] No se pudo abrir el archivo: " << inputPath << "\n";
        return 2;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const string source = buffer.str();

    if (looksLikeNonCppSyntax(source)) {
        std::cerr << "[ERROR] La fuente no es C++ del subconjunto aceptado "
                     "(use int main(), bool/std::string y std::cout << ... << std::endl).\n";
        return 1;
    }

    // Los archivos generados se guardan en el modo explicado o cuando se pide un prefijo con -o.
    string prefix = outputPrefixArg;
    if (prefix.empty() && explained) prefix = (fs::path("salida") / fs::path(inputPath).stem()).string();
    const bool writeFiles = !prefix.empty();

    std::ostream& log = std::cout;
    try {
        if (explained) {
            banner(log, "COMPILADOR DE C++17 - SEIS FASES");
            banner(log, "CODIGO FUENTE: " + fs::path(inputPath).filename().string());
            log << source << "\n";
        }

        // FASE 1: analisis lexico
        Scanner scanner(source);
        const vector<Token> tokens = scanner.scanAll();
        if (explained) {
            banner(log, "[1/6] ANALISIS LEXICO - SCANNER");
            printTokenDetails(tokens, log);
        }

        // FASE 2: analisis sintactico
        Parser parser(tokens);
        NP ast = parser.parseProgram();
        if (explained) {
            banner(log, "[2/6] ANALISIS SINTACTICO - PARSER");
            printAstTree(ast, log);
            log << "\n  Total de nodos del AST: " << countAstNodes(ast) << "\n";
        }

        // FASE 3: analisis semantico
        Semantic semantic;
        semantic.check(ast);
        if (explained) {
            banner(log, "[3/6] ANALISIS SEMANTICO");
            printSemanticSummary(ast, log);
        }

        // FASE 4: codigo intermedio
        IntermediateCodeGenerator irGenerator;
        IRProgram ir = irGenerator.generate(ast);
        const string irText = formatIR(ir);
        if (explained) {
            banner(log, "[4/6] GENERACION DE CODIGO INTERMEDIO - TAC");
            log << irText << "\n  Instrucciones TAC: " << ir.instructionCount() << "\n";
        }

        // FASE 5: optimizacion
        IROptimizer optimizer;
        IRProgram optimizedIR = optimizer.optimize(ir);
        const string optimizedText = formatIR(optimizedIR);
        if (explained) {
            const OptStats& s = optimizer.statistics();
            banner(log, "[5/6] OPTIMIZACION DE CODIGO INTERMEDIO");
            log << optimizedText << "\n"
                << "  Resumen de la optimizacion (" << s.rounds << " pasada(s)):\n"
                << "    constantes propagadas ........ " << s.propagated << "\n"
                << "    copias propagadas ............ " << s.copies << "\n"
                << "    operaciones plegadas ......... " << s.folded << "\n"
                << "    simplificaciones algebraicas . " << s.algebraic << "\n"
                << "    saltos condicionales resueltos " << s.branches << "\n"
                << "    codigo inalcanzable .......... " << s.unreachable << "\n"
                << "    saltos redundantes ........... " << s.jumps << "\n"
                << "    etiquetas sin uso ............ " << s.labels << "\n"
                << "    codigo muerto ................ " << s.deadCode << "\n"
                << "  Instrucciones: " << s.before << " -> " << s.after << "\n";
        }

        // FASE 6: codigo objeto binario CVM17 (es lo que ejecuta la maquina virtual)
        ObjectCodeGenerator objectGenerator;
        const vector<uint8_t> bytes = objectGenerator.generate(optimizedIR);
        const BCModule module = ObjectCodeGenerator::decode(bytes);    // se lee de vuelta desde los bytes
        if (explained) {
            banner(log, "[6/6] GENERACION DE CODIGO OBJETO BINARIO - CVM17");
            log << ObjectCodeGenerator::disassemble(module)
                << "\n  Primeros bytes del modulo:\n" << ObjectCodeGenerator::hexDump(bytes)
                << "  Tamano: " << bytes.size() << " bytes | verificacion binaria: correcta "
                << "(firma, version, constantes, funciones, operandos y saltos)\n";
        }
        if (writeFiles) {
            const fs::path prefixPath(prefix);
            if (prefixPath.has_parent_path()) fs::create_directories(prefixPath.parent_path());
            writeTextFile(prefix + ".ir", irText);
            writeTextFile(prefix + ".opt.ir", optimizedText);
            ObjectCodeGenerator::writeBinary(prefix + ".cobj", bytes);
            writeTextFile(prefix + ".obj.txt", ObjectCodeGenerator::disassemble(module));
            if (explained) {
                log << "  Archivos generados: " << prefix << ".ir, " << prefix << ".opt.ir, "
                    << prefix << ".cobj, " << prefix << ".obj.txt\n";
            }
        }

        // EJECUCION en la maquina virtual CVM17
        if (explained) {
            banner(log, "EJECUCION - MAQUINA VIRTUAL CVM17");
            log << "  --- inicio de la salida del programa ---\n";
        }
        VirtualMachine vm(module, std::cout);
        const int exitCode = vm.run();
        std::cout.flush();
        if (!std::cout) throw std::runtime_error("Fallo al escribir en la salida estandar.");
        if (explained) {
            log << "  --- fin de la salida del programa ---\n"
                << "  Instrucciones ejecutadas por la VM: " << vm.stepsExecuted() << "\n"
                << "  Codigo de salida de main: " << exitCode << "\n"
                << "\nCompilacion y ejecucion completadas con exito.\n";
        }
        return exitCode;   // como un ejecutable C++: el valor devuelto por main
    } catch (const std::exception& e) {
        std::cout.flush();
        std::cerr << "[ERROR] " << e.what() << "\n";
    } catch (...) {
        std::cout.flush();
        std::cerr << "[ERROR] Error fatal desconocido.\n";
    }
    return 1;
}

static int nativeFile(const string& inputPath, bool explained, const string& outputPath) {
    try {
        if (!fs::is_regular_file(inputPath)) {
            std::cerr << "[ERROR] No se pudo abrir el archivo: " << inputPath << "\n";
            return 2;
        }
        const string out = outputPath.empty() ? native_backend::defaultObjectPath(inputPath) : outputPath;
        std::ostringstream details;
        std::ostream& nativeLog = explained ? std::cout : static_cast<std::ostream&>(details);
        if (explained) banner(nativeLog, "BACKEND NATIVO C++17 - OBJETO DEL SISTEMA");
        const int result = native_backend::compileObject(inputPath, out, nativeLog);
        if (!explained && result == 0) std::cout << "Objeto generado: " << out << "\n";
        if (!explained && result != 0 && !details.str().empty()) std::cerr << details.str();
        return result;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
}

int main(int argc, char** argv) {
    string inputName, outputPath;
    bool nativeMode = false, all = false, listOnly = false;
    bool modeSelected = false, explained = false;
    for (int i = 1; i < argc; ++i) {
        const string arg = argv[i];
        if (arg == "--native") nativeMode = true;
        else if (arg == "--simple" || arg == "--salida-simple") { explained = false; modeSelected = true; }
        else if (arg == "--explained" || arg == "--explicada" || arg == "--salida-explicada") { explained = true; modeSelected = true; }
        else if (arg == "--todos" || arg == "--all") all = true;
        else if (arg == "--listar" || arg == "--list") listOnly = true;
        else if (arg == "--help" || arg == "-h") { std::cout << usage() << "\n"; return 0; }
        else if (arg == "-o") {
            if (i + 1 >= argc || !outputPath.empty()) { std::cerr << usage() << "\n"; return 2; }
            outputPath = argv[++i];
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "[ERROR] Opcion desconocida: " << arg << "\n" << usage() << "\n";
            return 2;
        } else if (inputName.empty()) inputName = arg;
        else { std::cerr << usage() << "\n"; return 2; }
    }
    if (all && (!inputName.empty() || !outputPath.empty())) {
        std::cerr << "[ERROR] --todos no se combina con un archivo ni con -o.\n";
        return 2;
    }

    const fs::path codesDir = findCodesDir(argv[0]);
    if (listOnly) {
        if (codesDir.empty()) { std::cerr << "[ERROR] No se encontro la carpeta 'codigos'.\n"; return 2; }
        for (const auto& f : listSources(codesDir)) std::cout << fs::path(f).filename().string() << "\n";
        return 0;
    }

    // Seleccion de archivos
    vector<string> targets;
    if (all || inputName.empty()) {
        if (codesDir.empty()) {
            std::cerr << "[ERROR] No se encontro la carpeta 'codigos'. Crea una carpeta llamada 'codigos' con tus archivos .cpp "
                         "o indica un archivo.\n";
            return 2;
        }
        const vector<string> files = listSources(codesDir);
        if (files.empty()) {
            std::cerr << "[ERROR] La carpeta '" << codesDir.string() << "' no contiene archivos .cpp.\n";
            return 2;
        }
        if (all || files.size() == 1) targets = files;
        else if (hasInteractiveInput()) {
            const string chosen = chooseFile(files);
            if (chosen.empty()) return 2;
            targets.push_back(chosen);
        } else {
            std::cerr << "[ERROR] Hay " << files.size() << " archivos en '" << codesDir.string()
                      << "'. Indica uno por nombre o usa --todos.\n";
            for (const auto& f : files) std::cerr << "  " << fs::path(f).filename().string() << "\n";
            return 2;
        }
    } else {
        const string resolved = resolveInput(inputName, codesDir);
        if (!isCppTranslationUnit(resolved)) {
            std::cerr << "[ERROR] La entrada debe ser un archivo C++ (.cpp, .cc, .cxx o .c++).\n";
            return 2;
        }
        targets.push_back(resolved);
    }

    if (!modeSelected) explained = hasInteractiveInput() ? askOutputMode() : false;

    int worst = 0;
    for (const string& target : targets) {
        if (targets.size() > 1) std::cout << "\n=== " << fs::path(target).filename().string() << " ===\n";
        const int rc = nativeMode ? nativeFile(target, explained, outputPath)
                                  : compileFile(target, explained, outputPath);
        if (worst == 0 && rc != 0) worst = rc;   // primer codigo distinto de cero
    }
    return worst;
}
