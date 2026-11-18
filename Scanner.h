#pragma once
#include "Lexer.h"
#include <stdexcept>
#include <cstdio>

// Fase 1 del compilador: Scanner administra el barrido completo de la fuente.
// Lexer conserva la responsabilidad de reconocer cada token individual.
class Scanner {
private:
    Lexer lexer;
public:
    explicit Scanner(const string& source) : lexer(source) {}

    vector<Token> scanAll() {
        vector<Token> tokens;
        for (;;) {
            Token token = lexer.getNextToken();
            if (token.type == UNKNOWN) {
                string detail = token.value;
                if (detail.size() == 1) {
                    const unsigned char ch = static_cast<unsigned char>(detail[0]);
                    if (ch >= 0x20 && ch < 0x7f) detail = "Caracter no reconocido '" + detail + "'";
                    else {
                        char buffer[80];
                        std::snprintf(buffer, sizeof buffer, "Caracter no reconocido (byte 0x%02X)", ch);
                        detail = buffer;
                    }
                }
                throw std::runtime_error("Error lexico (linea " + std::to_string(token.line) + "): " + detail);
            }
            tokens.push_back(token);
            if (token.type == END_OF_FILE) break;
        }
        return tokens;
    }
};
