#pragma once
#include "Lexer.h"
#include "AST.h"
#include <stdexcept>
#include <algorithm>
#include <iostream>

static bool endsWith(const string& s, const string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

[[maybe_unused]] static string elemOf(const string& t) {
    return endsWith(t, "[]") ? t.substr(0, t.size() - 2) : "";
}

class Parser {
private:
    vector<Token> toks;
    size_t p = 0;
    static constexpr int STREAM_OPERAND_PREC = 9; // primer nivel por encima de '<<' / '>>' (8)

    const Token& cur() const { return toks[p]; }
    const Token& at(size_t o) const { return toks[std::min(p + o, toks.size() - 1)]; }

    bool isSym(const string& v) const { return cur().type == SYMBOL && cur().value == v; }
    bool isOp(const string& v) const { return cur().type == OPERATOR && cur().value == v; }
    bool isKw(const string& v) const { return cur().type == KEYWORD && cur().value == v; }

    [[noreturn]] void fail(const string& expected = "") const {
        string v = (cur().type == END_OF_FILE) ? "fin de archivo" : cur().value;
        string m = "Error sintactico (linea " + to_string(cur().line) + "): Token inesperado '" + v + "'";
        if (!expected.empty()) m += ", se esperaba " + expected;
        throw std::runtime_error(m);
    }

    [[noreturn]] void failMsg(const string& msg) const {
        throw std::runtime_error("Error sintactico (linea " + to_string(cur().line) + "): " + msg);
    }

    void expectSym(const string& s) { if (!isSym(s)) fail("'" + s + "'"); p++; }
    void expectKw(const string& s) { if (!isKw(s)) fail("'" + s + "'"); p++; }
    string expectIdent() {
        if (cur().type != IDENTIFIER) fail("un identificador");
        return toks[p++].value;
    }

    void skipModifiers() {
        while (isKw("public") || isKw("private") || isKw("protected") || isKw("static")) p++;
    }

    bool isPrimKw(const Token& t, bool allowVoid) const {
        if (t.type != KEYWORD) return false;
        // Se reconocen algunos tipos adicionales para poder emitir un diagnostico
        // explicito de "tipo no soportado" en semantica, sin fingir que se ejecutan.
        return t.value == "int" || t.value == "double" || t.value == "bool" || t.value == "auto" ||
               t.value == "char" || t.value == "float" || t.value == "long" || t.value == "short" ||
               t.value == "unsigned" || t.value == "signed" || t.value == "wchar_t" ||
               (allowVoid && t.value == "void");
    }

    bool isStringTypeAt(size_t q) const {
        q = std::min(q, toks.size() - 1);
        if (toks[q].type == IDENTIFIER && toks[q].value == "string") return true;
        return toks[q].type == IDENTIFIER && toks[q].value == "std" &&
               toks[std::min(q + 1, toks.size() - 1)].type == OPERATOR && toks[std::min(q + 1, toks.size() - 1)].value == "::" &&
               toks[std::min(q + 2, toks.size() - 1)].type == IDENTIFIER && toks[std::min(q + 2, toks.size() - 1)].value == "string";
    }

    bool isTypeStartAt(size_t q, bool allowVoid = false) const {
        q = std::min(q, toks.size() - 1);
        return (toks[q].type == KEYWORD && isPrimKw(toks[q], allowVoid)) || isStringTypeAt(q);
    }

    string parseType() {
        string t;
        if (isStringTypeAt(p)) {
            if (cur().value == "std") p += 3; else p++;
            t = "std::string";
        } else if (cur().type == KEYWORD && isPrimKw(cur(), true)) {
            t = toks[p++].value;
            // No se combinan tipos (unsigned long, long long, etc.) en esta version.
            if ((t == "unsigned" || t == "signed" || t == "long" || t == "short") &&
                cur().type == KEYWORD && (cur().value == "int" || cur().value == "long" || cur().value == "char"))
                failMsg("el tipo compuesto '" + t + " " + cur().value + "' aun no esta implementado");
        } else {
            fail("un tipo soportado (int, double, bool, auto o std::string)");
        }
        while (isSym("[") && at(1).type == SYMBOL && at(1).value == "]") {
            p += 2; t += "[]";
        }
        return t;
    }

    bool isLocalDeclStart() const {
        size_t q = p;
        if (toks[q].type == KEYWORD && toks[q].value == "const") q++;
        if (q < toks.size() && toks[q].type == KEYWORD && toks[q].value == "const") q++;
        if (!isTypeStartAt(q, false)) return false;
        if (isStringTypeAt(q)) q += (toks[q].value == "std" ? 3 : 1);
        else q++;
        while (q < toks.size() && toks[q].type == SYMBOL && toks[q].value == "[") {
            if (q + 1 >= toks.size() || toks[q + 1].value != "]") break;
            q += 2;
        }
        if (q < toks.size() && toks[q].type == OPERATOR && (toks[q].value == "*" || toks[q].value == "&")) return true;
        return q < toks.size() && toks[q].type == IDENTIFIER;
    }

    // Operando de cout << ...: en C++ solo los operadores con mas precedencia que
    // '<<' (aritmeticos, unarios, llamadas) forman parte del operando. Cualquier
    // otro ('<', '==', '&', '=', '?:'...) se aplicaria al flujo y no es valido aqui.
    NP parseStreamExpr() {
        NP value = parseBinary(STREAM_OPERAND_PREC);
        if (cur().type == OPERATOR && !isOp("<<"))
            failMsg("el operador '" + cur().value + "' se aplicaria a std::cout; use parentesis: cout << (expresion)");
        if (isSym("?"))
            failMsg("el operador '?:' se aplicaria a std::cout; use parentesis: cout << (expresion)");
        return value;
    }

    static string canonicalOperator(string op) {
        if (op == "and") return "&&";
        if (op == "or") return "||";
        if (op == "not") return "!";
        if (op == "bitand") return "&";
        if (op == "bitor") return "|";
        if (op == "xor") return "^";
        if (op == "compl") return "~";
        if (op == "and_eq") return "&=";
        if (op == "or_eq") return "|=";
        if (op == "xor_eq") return "^=";
        if (op == "not_eq") return "!=";
        return op;
    }

    NP parseClass() {
        skipModifiers();
        expectKw("class");
        auto c = Node::mk(NK::Class, cur().line);
        c->name = expectIdent();
        expectSym("{");
        while (!isSym("}")) {
            if (cur().type == END_OF_FILE) fail("'}'");
            parseMember(c);
        }
        expectSym("}");
        expectSym(";"); // en C++, class/struct termina con punto y coma
        return c;
    }

    vector<NP> parseParams() {
        vector<NP> ps;
        expectSym("(");
        while (!isSym(")")) {
            bool paramConst = false;
            if (isKw("const")) { p++; paramConst = true; }
            auto d = Node::mk(NK::VarDecl, cur().line);
            d->flag = paramConst;
            d->type = parseType();
            if (isOp("*") || isOp("&")) failMsg("punteros y referencias aun no estan implementados en el interprete");
            d->name = expectIdent();
            ps.push_back(d);
            if (isSym(",")) p++; else break;
        }
        expectSym(")");
        return ps;
    }

    void parseMember(const NP& c) {
        skipModifiers();
        if (cur().type == IDENTIFIER && cur().value == c->name && at(1).type == SYMBOL && at(1).value == "(") {
            p++; parseParams(); parseBlock();
            return;
        }
        int ln = cur().line;
        string type = parseType();
        string name = expectIdent();
        if (isSym("(")) {
            auto m = Node::mk(NK::Method, ln);
            m->type = type; m->name = name; m->op = c->name;
            m->b = parseParams();
            m->a.push_back(parseBlock());
            c->a.push_back(m);
            return;
        }
        for (;;) {
            auto f = Node::mk(NK::Field, ln);
            f->type = type; f->name = name;
            if (isOp("=")) { p++; f->a.push_back(parseInit(type)); }
            c->a.push_back(f);
            if (isSym(",")) { p++; name = expectIdent(); continue; }
            break;
        }
        expectSym(";");
    }

    NP parseInit(const string& type) {
        return isSym("{") ? parseArrayInit(type) : parseExpr();
    }

    NP parseArrayInit(const string& type) {
        auto n = Node::mk(NK::ArrayInit, cur().line);
        n->type = type;
        expectSym("{");
        string et = elemOf(type);
        while (!isSym("}")) {
            if (isSym("{")) n->a.push_back(parseArrayInit(et));
            else n->a.push_back(parseExpr());
            if (isSym(",")) p++; else break;
        }
        expectSym("}");
        return n;
    }

    NP parseBlock() {
        auto b = Node::mk(NK::Block, cur().line);
        expectSym("{");
        while (!isSym("}")) {
            if (cur().type == END_OF_FILE) fail("'}'");
            b->a.push_back(parseStatement());
        }
        expectSym("}");
        return b;
    }

    NP parseLocalDecl() {
        int ln = cur().line;
        bool isConst = false;
        if (isKw("const")) { p++; isConst = true; }
        string type = parseType();
        if (isOp("*") || isOp("&")) failMsg("punteros y referencias aun no estan implementados en el interprete");
        auto blk = Node::mk(NK::Block, ln); blk->flag = true;
        for (;;) {
            auto d = Node::mk(NK::VarDecl, cur().line);
            d->type = type; d->name = expectIdent(); d->flag = isConst;
            if (isOp("=")) { p++; d->a.push_back(parseInit(type)); }
            blk->a.push_back(d);
            if (isSym(",")) { p++; continue; }
            break;
        }
        return blk->a.size() == 1 ? blk->a[0] : blk;
    }

    static bool isStatementExpr(const NP& e) {
        return e->k == NK::Assign || e->k == NK::IncDec || e->k == NK::Call ||
               e->k == NK::MethodCall || e->k == NK::NewObj;
    }

    NP parseExprStmt() {
        int ln = cur().line;
        auto e = parseExpr();
        if (!isStatementExpr(e)) failMsg("la expresion no es una sentencia valida");
        auto s = Node::mk(NK::ExprStmt, ln);
        s->a.push_back(e);
        return s;
    }

    NP parseExprList() {
        auto blk = Node::mk(NK::Block, cur().line); blk->flag = true;
        blk->a.push_back(parseExprStmt());
        while (isSym(",")) { p++; blk->a.push_back(parseExprStmt()); }
        return blk;
    }

    NP parseStatement() {
        int ln = cur().line;
        if (isSym("{")) return parseBlock();
        if (isSym(";")) { p++; auto b = Node::mk(NK::Block, ln); b->flag = true; return b; }
        // Salida propia de C++: cout << expresion << endl;
        const bool coutStart = cur().type == IDENTIFIER && cur().value == "cout" &&
                               at(1).type == OPERATOR && at(1).value == "<<";
        const bool stdCoutStart = cur().type == IDENTIFIER && cur().value == "std" &&
                                  at(1).type == OPERATOR && at(1).value == "::" &&
                                  at(2).type == IDENTIFIER && at(2).value == "cout" &&
                                  at(3).type == OPERATOR && at(3).value == "<<";
        if (coutStart || stdCoutStart) {
            if (stdCoutStart) p += 3; else ++p;
            auto output = Node::mk(NK::StreamOutput, ln);
            while (isOp("<<")) {
                ++p;
                const bool endl = (cur().type == IDENTIFIER && cur().value == "endl") ||
                    (cur().type == IDENTIFIER && cur().value == "std" &&
                     at(1).type == OPERATOR && at(1).value == "::" &&
                     at(2).type == IDENTIFIER && at(2).value == "endl");
                if (endl) {
                    p += (cur().value == "std" ? 3 : 1);
                    output->flag = true;
                    if (isOp("<<")) failMsg("std::endl debe ser el ultimo elemento de esta salida en el subconjunto educativo");
                    break;
                }
                output->a.push_back(parseStreamExpr());
            }
            if (output->a.empty() && !output->flag) failMsg("cout requiere al menos una expresion o std::endl");
            expectSym(";");
            return output;
        }
        if (cur().type == KEYWORD) {
            string v = cur().value;
            if (v == "switch") {
                p++; expectSym("(");
                auto n = Node::mk(NK::Switch, ln);
                n->a.push_back(parseExpr());
                expectSym(")"); expectSym("{");
                while (!isSym("}")) {
                    if (cur().type == END_OF_FILE) fail("'}'");
                    auto c = Node::mk(NK::Case, cur().line);
                    if (isKw("case")) {
                        p++;
                        c->a.push_back(parseExpr());
                        expectSym(":");
                    } else if (isKw("default")) {
                        p++; c->flag = true; expectSym(":");
                    } else {
                        failMsg("se esperaba una etiqueta 'case' o 'default' dentro de switch");
                    }
                    while (!isSym("}") && !isKw("case") && !isKw("default"))
                        c->a.push_back(parseStatement());
                    n->b.push_back(c);
                }
                expectSym("}");
                return n;
            }
            if (v == "case" || v == "default") failMsg("etiqueta '" + v + "' fuera de una sentencia switch");
            if (v == "if") {
                p++; expectSym("(");
                auto n = Node::mk(NK::If, ln);
                n->a.push_back(parseExpr());
                expectSym(")");
                n->a.push_back(parseStatement());
                if (isKw("else")) { p++; n->a.push_back(parseStatement()); }
                return n;
            }
            if (v == "while") {
                p++; expectSym("(");
                auto n = Node::mk(NK::While, ln);
                n->a.push_back(parseExpr());
                expectSym(")");
                n->a.push_back(parseStatement());
                return n;
            }
            if (v == "do") {
                p++;
                auto n = Node::mk(NK::DoWhile, ln);
                n->a.push_back(parseStatement());
                expectKw("while"); expectSym("(");
                n->a.push_back(parseExpr());
                expectSym(")"); expectSym(";");
                return n;
            }
            if (v == "for") {
                p++; expectSym("(");
                if (isLocalDeclStart()) {
                    size_t save = p;
                            if (isKw("const")) p++;
                    string t = parseType();
                    if (cur().type == IDENTIFIER && at(1).type == SYMBOL && at(1).value == ":") {
                        auto n = Node::mk(NK::ForEach, ln);
                        n->type = t; n->name = toks[p].value; p += 2;
                        n->a.push_back(parseExpr());
                        expectSym(")");
                        n->a.push_back(parseStatement());
                        return n;
                    }
                    p = save;
                }
                auto n = Node::mk(NK::For, ln);
                NP init = nullptr, cond = nullptr, upd = nullptr;
                if (!isSym(";")) init = isLocalDeclStart() ? parseLocalDecl() : parseExprList();
                expectSym(";");
                if (!isSym(";")) cond = parseExpr();
                expectSym(";");
                if (!isSym(")")) upd = parseExprList();
                expectSym(")");
                NP body = parseStatement();
                n->a = {init, cond, upd, body};
                return n;
            }
            if (v == "return") {
                p++;
                auto n = Node::mk(NK::Return, ln);
                if (!isSym(";")) n->a.push_back(parseExpr());
                expectSym(";");
                return n;
            }
            if (v == "break" || v == "continue") {
                p++; expectSym(";");
                return Node::mk(v == "break" ? NK::Break : NK::Continue, ln);
            }
            if (v == "throw") {
                p++;
                auto n = Node::mk(NK::Throw, ln);
                n->a.push_back(parseExpr());
                expectSym(";");
                return n;
            }
            if (v == "try") {
                p++;
                auto n = Node::mk(NK::Try, ln);
                n->a.push_back(parseBlock());
                NP fin = nullptr;
                while (isKw("catch")) {
                    p++;
                    auto c = Node::mk(NK::Catch, cur().line);
                    expectSym("(");
                            c->type = expectIdent();
                    while (isSym("|")) { p++; c->type += " " + expectIdent(); }
                    c->name = expectIdent();
                    expectSym(")");
                    c->a.push_back(parseBlock());
                    n->b.push_back(c);
                }
                if (n->b.empty()) failMsg("try necesita al menos un bloque catch en el subconjunto educativo C++17");
                n->a.push_back(fin);
                return n;
            }
        }
        if (isLocalDeclStart()) {
            auto d = parseLocalDecl();
            expectSym(";");
            return d;
        }
        auto s = parseExprStmt();
        expectSym(";");
        return s;
    }

    NP parseExpr() { return parseAssignment(); }

    NP parseAssignment() {
        NP lhs = parseTernary();
        if (cur().type == OPERATOR) {
            const string& v = cur().value;
            const string op = canonicalOperator(v);
            if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" ||
                op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=") {
                if (lhs->k != NK::Var && lhs->k != NK::Index)
                    failMsg("el lado izquierdo de la asignacion debe ser una variable o un arreglo");
                auto n = Node::mk(NK::Assign, cur().line);
                n->op = op; p++;
                n->a.push_back(lhs);
                n->a.push_back(parseAssignment());
                return n;
            }
        }
        return lhs;
    }

    NP parseTernary() {
        NP c = parseBinary(1);
        if (isSym("?")) {
            auto n = Node::mk(NK::Ternary, cur().line);
            p++;
            NP x = parseAssignment();
            expectSym(":");
            NP y = parseTernary();
            n->a = {c, x, y};
            return n;
        }
        return c;
    }

    int prec(const Token& t) const {
        if (t.type != OPERATOR) return 0;
        const string& v = t.value;
        if (v == "||" || v == "or") return 1;
        if (v == "&&" || v == "and") return 2;
        if (v == "|" || v == "bitor") return 3;
        if (v == "^" || v == "xor") return 4;
        if (v == "&" || v == "bitand") return 5;
        if (v == "==" || v == "!=" || v == "not_eq") return 6;
        if (v == "<" || v == ">" || v == "<=" || v == ">=") return 7;
        if (v == "<<" || v == ">>") return 8;
        if (v == "+" || v == "-") return 9;
        if (v == "*" || v == "/" || v == "%") return 10;
        return 0;
    }

    NP parseBinary(int minPrec) {
        NP lhs = parseUnary();
        for (;;) {
            int pr = prec(cur());
            if (pr == 0 || pr < minPrec) break;
            auto n = Node::mk(NK::Binary, cur().line);
            n->op = canonicalOperator(toks[p++].value);
            NP rhs = parseBinary(pr + 1);
            n->a = {lhs, rhs};
            lhs = n;
        }
        return lhs;
    }

    NP parseUnary() {
        int ln = cur().line;
        if (cur().type == OPERATOR) {
            const string v = cur().value;
            if (v == "!" || v == "not" || v == "-" || v == "+" || v == "~" || v == "compl") {
                p++;
                if (v == "-" && cur().type == NUMBER && cur().value == "2147483648") {
                    p++;
                    return Node::mkLit(Value::Int(-2147483648LL), ln);
                }
                auto n = Node::mk(NK::Unary, ln);
                n->op = canonicalOperator(v); n->a.push_back(parseUnary());
                return n;
            }
            if (v == "++" || v == "--") {
                p++;
                auto n = Node::mk(NK::IncDec, ln);
                n->op = v; n->flag = true;
                NP t = parseUnary();
                if (t->k != NK::Var && t->k != NK::Index) failMsg("'" + v + "' solo se puede aplicar a una variable");
                n->a.push_back(t);
                return n;
            }
        }
        if (isSym("(") && isPrimKw(at(1), false) && at(2).type == SYMBOL && at(2).value == ")") {
            auto n = Node::mk(NK::Cast, ln);
            n->type = at(1).value;
            p += 3;
            n->a.push_back(parseUnary());
            return n;
        }
        return parsePostfix();
    }

    vector<NP> parseArgs() {
        vector<NP> args;
        expectSym("(");
        while (!isSym(")")) {
            args.push_back(parseExpr());
            if (isSym(",")) p++; else break;
        }
        expectSym(")");
        return args;
    }

    NP parsePostfix() {
        NP e = parsePrimary();
        for (;;) {
            int ln = cur().line;
            if (isSym(".")) {
                p++;
                string nm = expectIdent();
                if (isSym("(")) {
                    auto m = Node::mk(NK::MethodCall, ln);
                    m->name = nm; m->a.push_back(e);
                    for (auto& x : parseArgs()) m->a.push_back(x);
                    e = m;
                } else {
                    auto m = Node::mk(NK::Member, ln);
                    m->name = nm; m->a.push_back(e);
                    e = m;
                }
            } else if (isSym("[")) {
                p++;
                auto ix = Node::mk(NK::Index, ln);
                ix->a.push_back(e);
                ix->a.push_back(parseExpr());
                expectSym("]");
                e = ix;
            } else if (cur().type == OPERATOR && (cur().value == "++" || cur().value == "--")) {
                if (e->k != NK::Var && e->k != NK::Index) failMsg("operador solo aplicable a variable");
                auto n = Node::mk(NK::IncDec, ln);
                n->op = toks[p++].value; n->flag = false;
                n->a.push_back(e);
                e = n;
            } else {
                break;
            }
        }
        return e;
    }

    NP parseNew() {
        int ln = cur().line;
        p++;
        string base;
        if (isPrimKw(cur(), false)) base = toks[p++].value; else base = expectIdent();
        
        if (isSym("[")) {
            auto n = Node::mk(NK::NewArray, ln);
            int total = 0;
            while (isSym("[")) {
                p++;
                if (isSym("]")) { p++; total++; }
                else { n->a.push_back(parseExpr()); expectSym("]"); total++; }
            }
            string t = base;
            for (int i = 0; i < total; i++) t += "[]";
            n->type = t;
            if (isSym("{")) {
                if (!n->a.empty()) fail();
                n->b.push_back(parseArrayInit(t));
            } else if (n->a.empty()) {
                fail("las dimensiones del arreglo");
            }
            return n;
        }
        if (isSym("(")) {
            auto n = Node::mk(NK::NewObj, ln);
            n->name = base;
            n->a = parseArgs();
            return n;
        }
        fail("se esperaba '[' o '('");
    }

    static string numericCore(string s, bool floating) {
        s.erase(std::remove(s.begin(), s.end(), '\''), s.end()); // separadores 1'000
        while (!s.empty() && (s.back() == 'u' || s.back() == 'U' || s.back() == 'l' || s.back() == 'L' ||
                              (floating && (s.back() == 'f' || s.back() == 'F')))) s.pop_back();
        return s;
    }

    NP parsePrimary() {
        const Token t = cur();
        int ln = t.line;
        switch (t.type) {
            case NUMBER: {
                long long v = 0;
                string raw = numericCore(t.value, false);
                const bool binary = raw.size() >= 2 && raw[0] == '0' && (raw[1] == 'b' || raw[1] == 'B');
                const string digits = binary ? raw.substr(2) : raw;
                size_t used = 0;
                try {
                    v = std::stoll(digits, &used, binary ? 2 : 0); // binario, o decimal/octal/hexadecimal
                } catch (const std::exception&) { failMsg("literal entero fuera de rango o mal formado: " + t.value); }
                // stoll se detiene en el primer caracter invalido (p. ej. el 8 de "08"): todo
                // el literal debe consumirse, como exige C++.
                if (used != digits.size()) failMsg("literal entero mal formado: " + t.value);
                if (v > 2147483647LL) failMsg("literal entero fuera del rango int del interprete: " + t.value);
                p++;
                return Node::mkLit(Value::Int(v), ln);
            }
            case DOUBLE_LIT: {
                double d = 0;
                string raw = numericCore(t.value, true);
                size_t used = 0;
                try { d = std::stod(raw, &used); }
                catch (const std::exception&) { failMsg("literal decimal fuera de rango o mal formado: " + t.value); }
                if (used != raw.size()) failMsg("literal decimal mal formado: " + t.value);
                if (std::isinf(d)) failMsg("literal decimal fuera de rango: " + t.value);
                p++;
                return Node::mkLit(Value::Dbl(d), ln);
            }
            case STRING_LIT:
                p++;
                return Node::mkLit(Value::Str(t.value), ln);
            case KEYWORD:
                if (t.value == "true") { p++; return Node::mkLit(Value::Bool(true), ln); }
                if (t.value == "false") { p++; return Node::mkLit(Value::Bool(false), ln); }
                if (t.value == "nullptr") failMsg("nullptr requiere soporte de punteros, aun no implementado");
                if (t.value == "null") { p++; return Node::mkLit(Value::Null(), ln); }
                if (t.value == "new") return parseNew();
                fail("una expresion");
            case IDENTIFIER:
                p++;
                if (isSym("(")) {
                    auto c = Node::mk(NK::Call, ln);
                    c->name = t.value; c->a = parseArgs();
                    return c;
                }
                {
                    auto v = Node::mk(NK::Var, ln);
                    v->name = t.value;
                    return v;
                }
            case SYMBOL:
                if (t.value == "(") {
                    p++;
                    NP e = parseExpr();
                    expectSym(")");
                    return e;
                }
                fail("una expresion");
            default:
                fail("una expresion");
        }
    }

public:
    explicit Parser(const vector<Token>& scannedTokens) : toks(scannedTokens) {
        if (toks.empty() || toks.back().type != END_OF_FILE)
            toks.push_back({END_OF_FILE, "", toks.empty() ? 1 : toks.back().line});
    }

    // Constructor de compatibilidad: permite usar Parser directamente con una fuente.
    Parser(const string& src) {
        Lexer lx(src);
        for (;;) {
            Token t = lx.getNextToken();
            if (t.type == UNKNOWN) {
                string d = t.value;
                if (d.size() == 1) {
                    unsigned char ch = (unsigned char)d[0];
                    if (ch >= 0x20 && ch < 0x7f) d = "Caracter no reconocido '" + d + "'";
                    else { char hx[80]; snprintf(hx, sizeof hx, "Caracter no reconocido (byte 0x%02X)", ch); d = hx; }
                }
                throw std::runtime_error("Error lexico (linea " + to_string(t.line) + "): " + d);
            }
            toks.push_back(t);
            if (t.type == END_OF_FILE) break;
        }
    }

    NP parseProgram() {
        auto prog = Node::mk(NK::Program, 1);
        while (cur().type == KEYWORD && cur().value == "using" && at(1).type == KEYWORD && at(1).value == "namespace" && at(2).type == IDENTIFIER && at(2).value == "std") { p += 3; expectSym(";"); }
        // C++: funciones globales (por ahora se admite una funcion main como punto de entrada).
        if (isTypeStartAt(p, true)) {
            auto c = Node::mk(NK::Class, cur().line); c->name = GLOBAL_UNIT_NAME;
            while (cur().type != END_OF_FILE) {
                if (cur().type == KEYWORD && cur().value == "using" && at(1).type == KEYWORD && at(1).value == "namespace" && at(2).type == IDENTIFIER && at(2).value == "std") { p += 3; expectSym(";"); continue; }
                int ln = cur().line;
                string ret = parseType();
                auto m = Node::mk(NK::Method, ln); m->type = ret; m->name = expectIdent(); m->op = c->name;
                m->b = parseParams();
                m->a.push_back(parseBlock());
                c->a.push_back(m);
            }
            prog->a.push_back(c);
        } else {
            while (cur().type != END_OF_FILE) prog->a.push_back(parseClass());
        }
        if (prog->a.empty()) fail("una funcion o clase C++");
        return prog;
    }
};
