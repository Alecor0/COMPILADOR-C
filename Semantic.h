#pragma once
#include "Parser.h"
#include "SymbolTable.h"
#include <climits>
#include <unordered_set>

static bool isNum(const string& t) { return t == "int" || t == "double" || t == "bool"; }
static string promote(const string& a, const string& b) { return (a == "double" || b == "double") ? "double" : "int"; }

static bool compat(const string& target, const string& src) {
    if (target == src) return true;
    // Conversiones escalares aritmeticas permitidas por C++ (int/double/bool).
    if (isNum(target) && isNum(src)) return true;
    if (target == "std::string" && src == "string_literal") return true;
    return false;
}

// Modo estricto: solo se acepta el subconjunto que el interprete sabe ejecutar.
// Todo lo demas se rechaza aqui, antes de ejecutar nada.
class Semantic {
private:
    SymbolTable symbols;
    string curRet;
    int loopDepth = 0;
    int breakDepth = 0;
    int scopeLevel = 0;
    struct FunctionInfo { NP node; string name; string ret; vector<string> params; };
    vector<FunctionInfo> functions;

    [[noreturn]] void err(const NP& n, const string& m) const {
        throw std::runtime_error("Error semantico (linea " + to_string(n->line) + "): " + m);
    }
    void mismatch(const NP& n, const string& to, const string& from) const {
        err(n, "Tipos incompatibles: no se puede convertir '" + from + "' a '" + to + "'.");
    }
    void opErr(const NP& n, const string& op, const string& l, const string& r) const {
        err(n, "El operador '" + op + "' no se puede aplicar a '" + l + "' y '" + r + "'.");
    }

    static bool integralConstant(const NP& n, long long& value) {
        if (!n) return false;
        if (n->k == NK::Lit) {
            if (n->val.t == Value::INT) { value = n->val.i; return true; }
            if (n->val.t == Value::BOOL) { value = n->val.b ? 1 : 0; return true; }
            return false;
        }
        if (n->k == NK::Unary && n->a.size() == 1) {
            long long x = 0;
            if (!integralConstant(n->a[0], x)) return false;
            if (n->op == "+") value = x;
            else if (n->op == "-") value = -x;
            else if (n->op == "~") value = ~x;
            else if (n->op == "!") value = !x;
            else return false;
            return value >= INT_MIN && value <= INT_MAX;
        }
        if (n->k != NK::Binary || n->a.size() != 2) return false;
        long long a = 0, b = 0;
        if (!integralConstant(n->a[0], a) || !integralConstant(n->a[1], b)) return false;
        // Los operandos recursivos ya estan limitados a int32; sus sumas,
        // restas y productos caben en int64_t, sin extensiones del compilador.
        long long r = 0;
        if (n->op == "+") r = a + b;
        else if (n->op == "-") r = a - b;
        else if (n->op == "*") r = a * b;
        else if (n->op == "/") { if (!b || (a == INT_MIN && b == -1)) return false; r = a / b; }
        else if (n->op == "%") { if (!b || (a == INT_MIN && b == -1)) return false; r = a % b; }
        else if (n->op == "&") r = a & b;
        else if (n->op == "|") r = a | b;
        else if (n->op == "^") r = a ^ b;
        else if (n->op == "<<") { if (b < 0 || b >= 31 || a < 0) return false; r = a * (1LL << static_cast<unsigned>(b)); }
        else if (n->op == ">>") { if (b < 0 || b >= 31) return false; r = a >> b; }
        else if (n->op == "==") r = a == b;
        else if (n->op == "!=") r = a != b;
        else if (n->op == "<") r = a < b;
        else if (n->op == ">") r = a > b;
        else if (n->op == "<=") r = a <= b;
        else if (n->op == ">=") r = a >= b;
        else return false;
        if (r < INT_MIN || r > INT_MAX) return false;
        value = static_cast<long long>(r);
        return true;
    }

    void checkType(const NP& n, const string& type, bool allowVoid) const {
        bool ok = type == "int" || type == "double" || type == "bool" || type == "std::string" ||
                  (allowVoid && type == "void");
        if (!ok) err(n, "Tipo desconocido o no soportado: '" + type + "'.");
    }

    string infer(const NP& n) {
        return inferCore(n);
    }

    string inferCore(const NP& n) {
        if (!n) throw std::logic_error("Semantic::infer recibio un nodo nulo");
        switch (n->k) {
            case NK::Lit:
                switch (n->val.t) {
                    case Value::INT: return "int";
                    case Value::DOUBLE: return "double";
                    case Value::BOOL: return "bool";
                    case Value::STR: return "string_literal";
                    case Value::NUL: return "null";
                    default: err(n, "Literal invalido.");
                }
            case NK::Var: {
                string t;
                if (symbols.lookup(n->name, t)) { return t; }
                err(n, "La variable '" + n->name + "' no ha sido declarada.");
            }
            case NK::Binary: {
                string l = infer(n->a[0]), r = infer(n->a[1]);
                const string& op = n->op;
                if (l == "void" || r == "void") opErr(n, op, l, r);
                if (op == "+" && ((l == "std::string" && (r == "std::string" || r == "string_literal")) || (r == "std::string" && l == "string_literal"))) return "std::string";
                if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
                    if (!isNum(l) || !isNum(r)) opErr(n, op, l, r);
                    if (op == "%" && (l == "double" || r == "double")) opErr(n, op, l, r);
                    return promote(l, r);
                }
                if (op == "<" || op == ">" || op == "<=" || op == ">=") {
                    if (!isNum(l) || !isNum(r)) opErr(n, op, l, r);
                    return "bool";
                }
                if (op == "==" || op == "!=") {
                    if ((isNum(l) && isNum(r)) ||
                        (l == "std::string" && (r == "std::string" || r == "string_literal")) ||
                        (r == "std::string" && l == "string_literal")) return "bool";
                    opErr(n, op, l, r);
                }
                if (op == "&&" || op == "||") {
                    if (!isNum(l) || !isNum(r)) opErr(n, op, l, r);
                    return "bool";
                }
                if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
                    if ((l != "int" && l != "bool") || (r != "int" && r != "bool")) opErr(n, op, l, r);
                    return "int";
                }
                err(n, "Operador no soportado: '" + op + "'.");
            }
            case NK::Unary: {
                string t = infer(n->a[0]);
                if (n->op == "!") { if (!isNum(t)) opErr(n, n->op, t, t); return "bool"; }
                if (n->op == "~") { if (t != "int" && t != "bool") opErr(n, n->op, t, t); return "int"; }
                if (n->op == "-" || n->op == "+") { if (!isNum(t)) opErr(n, n->op, t, t); return t == "bool" ? "int" : t; }
                err(n, "Operador unario no soportado: '" + n->op + "'.");
            }
            case NK::Assign: {
                if (n->a[0]->k != NK::Var) err(n, "Solo se puede asignar a variables simples.");
                string lt = infer(n->a[0]), rt = infer(n->a[1]);
                if (symbols.isConst(n->a[0]->name)) err(n, "No se puede modificar la variable const '" + n->a[0]->name + "'.");
                if (rt == "void") mismatch(n, lt, rt);
                if (n->op == "=") {
                    if (!compat(lt, rt)) mismatch(n, lt, rt);
                } else if (n->op == "+=" && lt == "std::string" && (rt == "std::string" || rt == "string_literal")) {
                    // std::string += std::string
                } else if (n->op == "&=" || n->op == "|=" || n->op == "^=" || n->op == "<<=" || n->op == ">>=") {
                    if ((lt != "int" && lt != "bool") || (rt != "int" && rt != "bool")) opErr(n, n->op, lt, rt);
                } else if (!isNum(lt) || !isNum(rt)) {
                    opErr(n, n->op, lt, rt);
                }
                return lt;
            }
            case NK::IncDec: {
                if (n->a[0]->k != NK::Var) err(n, "'" + n->op + "' solo se puede aplicar a variables simples.");
                string t = infer(n->a[0]);
                if (symbols.isConst(n->a[0]->name)) err(n, "No se puede modificar la variable const '" + n->a[0]->name + "'.");
                if (t != "int" && t != "double") opErr(n, n->op, t, t);
                return t;
            }
            case NK::Ternary: {
                string c = infer(n->a[0]);
                if (!isNum(c)) err(n, "La condicion del operador ternario debe ser escalar, no '" + c + "'.");
                string x = infer(n->a[1]), y = infer(n->a[2]);
                if (x == "void" || x != y) err(n, "Ambas ramas del operador ternario deben tener el mismo tipo ('" + x + "' vs '" + y + "').");
                return x;
            }
            case NK::Cast: {
                string t = infer(n->a[0]);
                if ((n->type != "int" && n->type != "double") || !isNum(t))
                    err(n, "Conversion no soportada de '" + t + "' a '" + n->type + "'.");
                return n->type;
            }
            case NK::Call: {
                return resolveCall(n).ret;
            }
            case NK::MethodCall:
                err(n, "Las llamadas a metodos miembro no estan implementadas en el modo educativo C++17.");
            default:
                err(n, string("Expresion no soportada: ") + nkName(n->k) + ".");
        }
    }


    const FunctionInfo& resolveCall(const NP& n) {
        vector<string> args;
        for (const auto& a : n->a) args.push_back(infer(a));
        const FunctionInfo* best = nullptr;
        int bestScore = -1;
        bool ambiguous = false;
        for (const auto& fn : functions) {
            if (fn.name != n->name || fn.name == "main" || fn.params.size() != args.size()) continue;
            int score = 0;
            bool viable = true;
            for (size_t i = 0; i < args.size(); ++i) {
                if (!compat(fn.params[i], args[i])) { viable = false; break; }
                score += (fn.params[i] == args[i]) ? 4 : 1;
            }
            if (!viable) continue;
            if (score > bestScore) { best = &fn; bestScore = score; ambiguous = false; }
            else if (score == bestScore) ambiguous = true;
        }
        if (!best) err(n, "No existe una sobrecarga compatible de la funcion '" + n->name + "' para " + to_string(args.size()) + " argumento(s).");
        if (ambiguous) err(n, "Llamada ambigua a la funcion sobrecargada '" + n->name + "'.");
        return *best;
    }

    static bool alwaysReturns(const NP& n) {
        if (!n) return false;
        if (n->k == NK::Return) return true;
        if (n->k == NK::Block) {
            for (const auto& child : n->a) if (alwaysReturns(child)) return true;
            return false;
        }
        if (n->k == NK::If && n->a.size() == 3)
            return alwaysReturns(n->a[1]) && alwaysReturns(n->a[2]);
        if (n->k == NK::Switch) {
            bool hasDefault = false;
            for (const auto& c : n->b) if (c->flag) hasDefault = true;
            if (!hasDefault || n->b.empty()) return false;
            // Desde cada etiqueta se puede caer a las siguientes (fall-through).
            // Cada posible punto de entrada debe alcanzar un return antes de un
            // break o del final del switch para que el switch garantice retorno.
            for (size_t start = 0; start < n->b.size(); ++start) {
                bool returns = false;
                for (size_t i = start; i < n->b.size() && !returns; ++i) {
                    const NP& section = n->b[i];
                    const size_t firstBody = section->flag ? 0 : 1;
                    for (size_t j = firstBody; j < section->a.size(); ++j) {
                        const NP& statement = section->a[j];
                        if (alwaysReturns(statement)) { returns = true; break; }
                        if (statement && statement->k == NK::Break) return false;
                    }
                }
                if (!returns) return false;
            }
            return true;
        }
        return false;
    }

    void cond(const NP& e) {
        string t = infer(e);
        if (!isNum(t)) err(e, "La condicion debe ser escalar (bool, int o double), no '" + t + "'.");
    }

    static bool isDecl(const NP& s) {
        if (!s) return false;
        if (s->k == NK::VarDecl) return true;
        return s->k == NK::Block && s->flag && !s->a.empty() && s->a[0]->k == NK::VarDecl;
    }

    void body(const NP& s) {
        if (isDecl(s)) err(s, "Una declaracion de variable no puede ser el cuerpo de una sentencia.");
        checkStmt(s);
    }

    void checkStmt(const NP& n) {
        if (!n) return;
        switch (n->k) {
            case NK::Block:
                if (!n->flag) { symbols.push(); scopeLevel++; }
                for (auto& s : n->a) checkStmt(s);
                if (!n->flag) { scopeLevel--; symbols.pop(); }
                break;
            case NK::VarDecl:
                if (n->type == "auto") {
                    if (n->a.empty()) err(n, "'auto' requiere un inicializador para deducir el tipo.");
                    string deduced = infer(n->a[0]);
                    if (deduced != "int" && deduced != "double" && deduced != "bool" && deduced != "std::string")
                        err(n, "No se puede deducir un tipo ejecutable para 'auto' a partir de '" + deduced + "'.");
                    n->type = deduced;
                }
                checkType(n, n->type, false);
                if (n->flag && n->a.empty()) err(n, "Una variable const debe inicializarse en su declaracion.");
                if (!n->a.empty()) {
                    string t = infer(n->a[0]);
                    if (!compat(n->type, t)) mismatch(n, n->type, t);
                }
                if (!symbols.insert(n->name, n->type, n->flag))
                    err(n, "La variable '" + n->name + "' ya ha sido declarada previamente.");
                break;
            case NK::ExprStmt:
                infer(n->a[0]);
                break;
            case NK::StreamOutput:
                for (const auto& value : n->a) {
                    if (infer(value) == "void") err(value, "cout no puede insertar una expresion de tipo void.");
                }
                break;
            case NK::If:
                cond(n->a[0]);
                body(n->a[1]);
                if (n->a.size() > 2) body(n->a[2]);
                break;
            case NK::While:
                cond(n->a[0]);
                loopDepth++; breakDepth++; body(n->a[1]); breakDepth--;
                break;
            case NK::DoWhile:
                loopDepth++; breakDepth++; body(n->a[0]); breakDepth--;
                cond(n->a[1]);
                break;
            case NK::For:
                symbols.push(); scopeLevel++;
                checkStmt(n->a[0]);
                if (n->a[1]) cond(n->a[1]);
                checkStmt(n->a[2]);
                loopDepth++; breakDepth++; body(n->a[3]); breakDepth--; loopDepth--;
                scopeLevel--; symbols.pop();
                break;
            case NK::Return:
                if (curRet == "void") {
                    if (!n->a.empty()) err(n, "Un metodo 'void' no puede devolver un valor.");
                } else {
                    if (n->a.empty()) err(n, "El metodo debe devolver un valor de tipo '" + curRet + "'.");
                    string t = infer(n->a[0]);
                    if (!compat(curRet, t)) mismatch(n, curRet, t);
                }
                break;
            case NK::Break:
                if (breakDepth == 0) err(n, "'break' fuera de un bucle o switch.");
                break;
            case NK::Continue:
                if (loopDepth == 0) err(n, "'continue' fuera de un bucle.");
                break;
            case NK::Switch: {
                if (n->a.size() != 1) err(n, "Sentencia switch mal formada.");
                string selectorType = infer(n->a[0]);
                if (selectorType != "int" && selectorType != "bool")
                    err(n, "La expresion de switch debe ser integral (int o bool), no '" + selectorType + "'.");
                std::unordered_set<long long> seenCases;
                bool seenDefault = false;
                symbols.push(); scopeLevel++; breakDepth++;
                for (const auto& c : n->b) {
                    if (!c || c->k != NK::Case) err(n, "Etiqueta case mal formada.");
                    size_t firstBody = 0;
                    if (c->flag) {
                        if (seenDefault) err(c, "Etiqueta default duplicada en switch.");
                        seenDefault = true;
                    } else {
                        if (c->a.empty()) err(c, "Etiqueta case sin valor.");
                        string caseType = infer(c->a[0]);
                        if (caseType != "int" && caseType != "bool")
                            err(c->a[0], "La etiqueta case debe ser una constante integral.");
                        long long value = 0;
                        if (!integralConstant(c->a[0], value))
                            err(c->a[0], "La etiqueta case debe ser una expresion constante integral.");
                        // bool sufre promocion integral a int en el control de switch;
                        // case 2 no se convierte a true y no colisiona con case 1.
                        if (!seenCases.insert(value).second)
                            err(c, "Valor case duplicado: " + to_string(value) + ".");
                        firstBody = 1;
                    }
                    for (size_t i = firstBody; i < c->a.size(); ++i) checkStmt(c->a[i]);
                }
                breakDepth--; scopeLevel--; symbols.pop();
                break;
            }
            default:
                err(n, string("Sentencia no soportada: ") + nkName(n->k) + ".");
        }
    }

public:

    void check(const NP& prog) {
        if (!prog || prog->k != NK::Program) throw std::runtime_error("Error semantico: AST invalido.");
        functions.clear(); scopeLevel = 0;
        std::unordered_set<string> signatures;
        int mains = 0;

        // Primera pasada: registrar todas las firmas, para permitir llamadas antes de la definicion.
        for (const auto& c : prog->a) {
            if (c->name != "Programa") err(c, "Las clases C++ aun no estan implementadas; esta version admite funciones globales.");
            for (const auto& m : c->a) {
                if (m->k != NK::Method)
                    err(m, string("Miembro no soportado: ") + nkName(m->k) + " '" + m->name + "'.");
                checkType(m, m->type, true);
                string sig = m->name + "(";
                FunctionInfo info; info.node = m; info.name = m->name; info.ret = m->type;
                for (size_t i = 0; i < m->b.size(); ++i) {
                    checkType(m->b[i], m->b[i]->type, false);
                    if (i) sig += ",";
                    sig += m->b[i]->type;
                    info.params.push_back(m->b[i]->type);
                }
                sig += ")";
                if (!signatures.insert(sig).second) err(m, "La funcion '" + sig + "' ya esta definida.");
                functions.push_back(info);
            }
        }

        // Segunda pasada: validar parametros, cuerpos, retornos y punto de entrada.
        for (const auto& c : prog->a) {
            for (const auto& m : c->a) {
                checkType(m, m->type, true);
                const bool isMain = m->name == "main";
                symbols.clear(); symbols.push();
                if (isMain) {
                    if (m->type != "int" || !m->b.empty())
                        err(m, "El punto de entrada debe ser exactamente 'int main()'.");
                    mains++;
                }
                for (const auto& param : m->b) {
                    if (!symbols.insert(param->name, param->type, param->flag)) err(param, "Parametro duplicado '" + param->name + "'.");
                }
                curRet = m->type; loopDepth = 0; breakDepth = 0;
                // El bloque externo de una funcion comparte ambito con sus parametros.
                m->a[0]->flag = true;
                checkStmt(m->a[0]);
                if (!isMain && m->type != "void" && !alwaysReturns(m->a[0]))
                    err(m, "La funcion no garantiza devolver un valor de tipo '" + m->type + "' en todos los caminos.");
                symbols.pop();
            }
        }
        if (mains != 1)
            throw std::runtime_error("Error semantico: se requiere exactamente una funcion 'int main()' (se encontraron " +
                                     to_string(mains) + ").");
    }
};
