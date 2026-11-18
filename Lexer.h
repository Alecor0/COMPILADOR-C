#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <cctype>

using std::string;
using std::vector;

enum TokenType {
    KEYWORD, IDENTIFIER, NUMBER, DOUBLE_LIT, STRING_LIT,
    OPERATOR, SYMBOL, END_OF_FILE, UNKNOWN
};

struct Token {
    TokenType type;
    string value;
    int line = 1;
};

class Lexer {
private:
    string source;
    size_t pos;
    int line;
    bool badComment = false;
    int commentLine = 0;
    // Palabras reservadas de C++17 (incluye las palabras de soporte del lenguaje).
    // Los tokens alternativos and/or/not/bitand/... se clasifican como operadores.
    std::unordered_set<string> keywords = {
        "alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch",
        "char", "char16_t", "char32_t", "class", "const", "constexpr", "const_cast",
        "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast",
        "else", "enum", "explicit", "export", "extern", "false", "float", "for",
        "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace",
        "new", "noexcept", "nullptr", "operator", "private", "protected", "public",
        "register", "reinterpret_cast", "return", "short", "signed", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
        "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned",
        "using", "virtual", "void", "volatile", "wchar_t", "while"
    };
    std::unordered_set<string> alternativeOperators = {
        "and", "and_eq", "bitand", "bitor", "compl", "not", "not_eq", "or", "or_eq", "xor", "xor_eq"
    };

    char peek(size_t o = 0) const {
        return (pos + o >= source.size()) ? '\0' : source[pos + o];
    }

    char advance() {
        if (pos >= source.size()) return '\0';
        char c = source[pos++];
        if (c == '\n') line++;
        return c;
    }

    void skipWhitespace() {
        while (pos < source.size()) {
            if (peek() == '#') { while (pos < source.size() && peek() != '\n') advance(); }
            else if (std::isspace((unsigned char)peek())) {
                advance();
            } else if (peek() == '/' && peek(1) == '/') {
                while (pos < source.size() && peek() != '\n') advance();
            } else if (peek() == '/' && peek(1) == '*') {
                commentLine = line;
                advance(); advance();
                while (pos < source.size() && !(peek() == '*' && peek(1) == '/')) advance();
                if (pos >= source.size()) { badComment = true; return; }
                advance(); advance();
            } else {
                break;
            }
        }
    }

public:
    Lexer(const string& src) : source(src), pos(0), line(1) {}

    Token getNextToken() {
        skipWhitespace();
        if (badComment) {
            badComment = false;
            return {UNKNOWN, "comentario de bloque sin cerrar", commentLine};
        }
        int ln = line;
        if (pos >= source.size()) return {END_OF_FILE, "", ln};

        char c = peek();

        if (std::isalpha((unsigned char)c) || c == '_' || c == '$') {
            string lexeme;
            while (std::isalnum((unsigned char)peek()) || peek() == '_' || peek() == '$') {
                lexeme += advance();
            }
            if (alternativeOperators.count(lexeme)) return {OPERATOR, lexeme, ln};
            if (keywords.count(lexeme)) return {KEYWORD, lexeme, ln};
            return {IDENTIFIER, lexeme, ln};
        }

        if (std::isdigit((unsigned char)c) || (c == '.' && std::isdigit((unsigned char)peek(1)))) {
            string lexeme;
            bool floating = false;
            auto takeDigits = [&](bool (*pred)(int)) {
                while (pred((unsigned char)peek()) || (peek() == '\'' && pred((unsigned char)peek(1)))) {
                    if (peek() == '\'') advance(); else lexeme += advance();
                }
            };
            if (c == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                lexeme += advance(); lexeme += advance();
                takeDigits([](int x){ return std::isxdigit(x) != 0; });
                if (peek() == '.') { floating = true; lexeme += advance(); takeDigits([](int x){ return std::isxdigit(x) != 0; }); }
                if (peek() == 'p' || peek() == 'P') {
                    floating = true; lexeme += advance();
                    if (peek() == '+' || peek() == '-') lexeme += advance();
                    takeDigits([](int x){ return std::isdigit(x) != 0; });
                }
            } else if (c == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
                lexeme += advance(); lexeme += advance();
                takeDigits([](int x){ return x == '0' || x == '1'; });
            } else {
                if (c == '.') { floating = true; lexeme += advance(); }
                takeDigits([](int x){ return std::isdigit(x) != 0; });
                if (peek() == '.' && peek(1) != '.') { floating = true; lexeme += advance(); takeDigits([](int x){ return std::isdigit(x) != 0; }); }
                if (peek() == 'e' || peek() == 'E') {
                    floating = true; lexeme += advance();
                    if (peek() == '+' || peek() == '-') lexeme += advance();
                    takeDigits([](int x){ return std::isdigit(x) != 0; });
                }
            }
            // Sufijos estándar: U, L, LL, F y combinaciones de U/L.
            bool floatSuffix = false;
            while (std::isalpha((unsigned char)peek())) {
                char x = peek();
                if (x == 'u' || x == 'U' || x == 'l' || x == 'L' || x == 'f' || x == 'F') {
                    if (x == 'f' || x == 'F') floatSuffix = true;
                    lexeme += advance();
                } else break;
            }
            if (floatSuffix) floating = true;
            return {floating ? DOUBLE_LIT : NUMBER, lexeme, ln};
        }

        if (c == '"' || c == '\'') {
            char q = advance(); string s;
            while (pos < source.size() && peek() != q && peek() != '\n') {
                char ch = advance();
                if (ch == '\\') {
                    char e = advance();
                    switch (e) {
                        case 'n': s += '\n'; break;
                        case 't': s += '\t'; break;
                        case 'r': s += '\r'; break;
                        case '0': s += '\0'; break;
                        default: s += e; break;
                    }
                } else {
                    s += ch;
                }
            }
            if (peek() != q) return {UNKNOWN, "literal sin cerrar", ln};
            advance();
            if (q == '\'' && s.size() != 1) return {UNKNOWN, "literal de caracter invalido (debe tener exactamente un caracter)", ln};
            return {STRING_LIT, s, ln};
        }

        // Operadores ordenados de mayor a menor longitud (maximal munch).
        static const char* ops[] = {
            "%:%:", "->*", "<<=", ">>=", "...", "##", "::", "->", ".*", "++", "--",
            "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "*=", "/=", "%=", "+=", "-=",
            "&=", "^=", "|=", "<%", "%>", "<:", ":>", "%:"
        };
        for (const char* op : ops) {
            size_t n = std::char_traits<char>::length(op);
            bool match = true;
            for (size_t i = 0; i < n; ++i) if (peek(i) != op[i]) { match = false; break; }
            if (match) { for (size_t i = 0; i < n; ++i) advance(); return {OPERATOR, op, ln}; }
        }

        if (string("=+-*/%<>!&|^~").find(c) != string::npos) return {OPERATOR, string(1, advance()), ln};
        if (string("(){}[];,.?:#").find(c) != string::npos) return {SYMBOL, string(1, advance()), ln};

        return {UNKNOWN, string(1, advance()), ln};
    }
};
