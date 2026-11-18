#pragma once
// Fase 4: generacion de codigo intermedio de tres direcciones (TAC).
//  - Las variables se renombran de forma unica por funcion (x, x.1, x.2...), de modo
//    que el IR ya no necesita ambitos en tiempo de ejecucion.
//  - Los operandos son estructurados (variable, temporal o constante), no cadenas.
//  - && y || se compilan como saltos (cortocircuito); cada argumento de cout se
//    imprime justo despues de evaluarse, igual que en C++17.
#include "AST.h"
#include "Runtime.h"
#include <sstream>
#include <iomanip>
#include <map>
#include <stdexcept>

enum class IROp : uint8_t {
    Label = 1, Declare, Move, Binary, Unary, IncDec, Cast, Call, Return,
    Print, PrintLn, Jump, JumpFalse, JumpTrue, Nop
};

struct Operand {
    enum Kind : uint8_t { None, Var, Temp, Const } kind = None;
    string name;
    Value val;

    static Operand var(const string& n) { Operand o; o.kind = Var; o.name = n; return o; }
    static Operand temp(const string& n) { Operand o; o.kind = Temp; o.name = n; return o; }
    static Operand constant(const Value& v) { Operand o; o.kind = Const; o.val = v; return o; }
    bool isNone() const { return kind == None; }
    bool isConst() const { return kind == Const; }
    bool isName() const { return kind == Var || kind == Temp; }
};

struct IRInstr {
    IROp op = IROp::Nop;
    Operand dst, a, b;
    string text;            // operador, tipo o nombre de funcion
    string label;           // etiqueta (Label) o destino (Jump*)
    vector<Operand> args;   // argumentos de Call
    bool flag = false;      // IncDec: true = prefijo
    int line = 0;
};

struct IRFunction {
    string name, ret;
    vector<std::pair<string, string>> params;   // (tipo, nombre unico)
    vector<IRInstr> code;
    int line = 0;
};

struct IRProgram {
    vector<IRFunction> functions;
    size_t instructionCount() const {
        size_t n = 0;
        for (const auto& f : functions) n += f.code.size();
        return n;
    }
};

static string quoteText(const string& s) {
    string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out + "\"";
}

static string operandText(const Operand& o) {
    switch (o.kind) {
        case Operand::None: return "_";
        case Operand::Var:
        case Operand::Temp: return o.name;
        case Operand::Const:
            switch (o.val.t) {
                case Value::INT: return "#i:" + std::to_string(o.val.i);
                case Value::DOUBLE: return "#d:" + fmtDouble(o.val.d);
                case Value::BOOL: return string("#b:") + (o.val.b ? "true" : "false");
                case Value::STR: return "#s:" + quoteText(o.val.s);
                case Value::V_VOID: return "#void";
                default: return "#null";
            }
    }
    return "?";
}

static const char* irOpName(IROp op) {
    switch (op) {
        case IROp::Label: return "LABEL";
        case IROp::Declare: return "DECLARE";
        case IROp::Move: return "MOVE";
        case IROp::Binary: return "BINARY";
        case IROp::Unary: return "UNARY";
        case IROp::IncDec: return "INCDEC";
        case IROp::Cast: return "CAST";
        case IROp::Call: return "CALL";
        case IROp::Return: return "RETURN";
        case IROp::Print: return "PRINT";
        case IROp::PrintLn: return "PRINTLN";
        case IROp::Jump: return "JUMP";
        case IROp::JumpFalse: return "JUMP_FALSE";
        case IROp::JumpTrue: return "JUMP_TRUE";
        default: return "NOP";
    }
}

class IntermediateCodeGenerator {
private:
    IRFunction* fn = nullptr;
    unsigned tempCounter = 0, labelCounter = 0;
    struct LoopLabels { string breakLabel, continueLabel; };
    vector<LoopLabels> loops;
    vector<std::map<string, string>> scopes;
    std::map<string, int> usedNames;

    string newLabel(const string& prefix) { return prefix + "_" + std::to_string(++labelCounter); }
    Operand newTemp() { return Operand::temp("%t" + std::to_string(++tempCounter)); }

    // ---- ambitos: cada declaracion recibe un nombre unico dentro de la funcion ----
    string declareName(const string& name) {
        int n = usedNames[name]++;
        string unique = n == 0 ? name : name + "." + std::to_string(n);
        scopes.back()[name] = unique;
        return unique;
    }
    string resolve(const string& name, int line) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second;
        }
        throw std::runtime_error("Generacion IR (linea " + std::to_string(line) + "): variable '" + name + "' sin declarar.");
    }

    IRInstr& emit(IROp op, int line) {
        fn->code.emplace_back();
        fn->code.back().op = op;
        fn->code.back().line = line;
        return fn->code.back();
    }
    void emitLabel(const string& l, int line) { emit(IROp::Label, line).label = l; }
    void emitJump(IROp op, const string& target, const Operand& cond, int line) {
        IRInstr& i = emit(op, line);
        i.label = target;
        i.a = cond;
    }
    void emitMove(const Operand& dst, const Operand& src, int line) {
        IRInstr& i = emit(IROp::Move, line);
        i.dst = dst; i.a = src;
    }

    // ---- ayudas para preservar el orden de evaluacion ----
    static bool hasSideEffect(const NP& n) {
        if (!n) return false;
        if (n->k == NK::Assign || n->k == NK::IncDec) return true;
        for (const auto& c : n->a) if (hasSideEffect(c)) return true;
        for (const auto& c : n->b) if (hasSideEffect(c)) return true;
        return false;
    }
    Operand snapshot(const Operand& v, int line) {
        Operand t = newTemp();
        emitMove(t, v, line);
        return t;
    }

    Operand expression(const NP& n) {
        if (!n) throw std::runtime_error("Generacion IR: expresion vacia.");
        switch (n->k) {
            case NK::Lit:
                if (n->val.t != Value::INT && n->val.t != Value::DOUBLE && n->val.t != Value::BOOL && n->val.t != Value::STR)
                    throw std::runtime_error("Generacion IR (linea " + std::to_string(n->line) + "): literal no soportado.");
                return Operand::constant(n->val);
            case NK::Var:
                return Operand::var(resolve(n->name, n->line));
            case NK::Binary: {
                if (n->a.size() != 2) throw std::runtime_error("Generacion IR: nodo binario incompleto.");
                if (n->op == "&&" || n->op == "||") {
                    const bool isAnd = n->op == "&&";
                    Operand dst = newTemp();
                    const string decisive = newLabel(isAnd ? "and_false" : "or_true");
                    const string end = newLabel("logic_end");
                    const IROp jump = isAnd ? IROp::JumpFalse : IROp::JumpTrue;
                    emitJump(jump, decisive, expression(n->a[0]), n->line);
                    emitJump(jump, decisive, expression(n->a[1]), n->line);
                    emitMove(dst, Operand::constant(Value::Bool(isAnd)), n->line);
                    emitJump(IROp::Jump, end, Operand(), n->line);
                    emitLabel(decisive, n->line);
                    emitMove(dst, Operand::constant(Value::Bool(!isAnd)), n->line);
                    emitLabel(end, n->line);
                    return dst;
                }
                BinOp bop;
                if (!binOpFromText(n->op, bop))
                    throw std::runtime_error("Generacion IR (linea " + std::to_string(n->line) + "): operador no soportado '" + n->op + "'.");
                Operand l = expression(n->a[0]);
                if (l.kind == Operand::Var && hasSideEffect(n->a[1])) l = snapshot(l, n->line);
                Operand r = expression(n->a[1]);
                IRInstr& i = emit(IROp::Binary, n->line);
                i.dst = newTemp(); i.a = l; i.b = r; i.text = n->op;
                return i.dst;
            }
            case NK::Unary: {
                UnOp uop;
                if (!unOpFromText(n->op, uop))
                    throw std::runtime_error("Generacion IR (linea " + std::to_string(n->line) + "): operador unario no soportado '" + n->op + "'.");
                Operand v = expression(n->a.at(0));
                IRInstr& i = emit(IROp::Unary, n->line);
                i.dst = newTemp(); i.a = v; i.text = n->op;
                return i.dst;
            }
            case NK::Cast: {
                if (typeFromName(n->type) == T_NONE)
                    throw std::runtime_error("Generacion IR: tipo de conversion no soportado '" + n->type + "'.");
                Operand v = expression(n->a.at(0));
                IRInstr& i = emit(IROp::Cast, n->line);
                i.dst = newTemp(); i.a = v; i.text = n->type;
                return i.dst;
            }
            case NK::Assign: {
                const NP& target = n->a.at(0);
                if (target->k != NK::Var) throw std::runtime_error("Generacion IR: se esperaba una variable como destino de la asignacion.");
                Operand tgt = Operand::var(resolve(target->name, target->line));
                if (n->op == "=") {
                    Operand value = expression(n->a.at(1));
                    emitMove(tgt, value, n->line);
                    return tgt;
                }
                BinOp bop;
                const string baseOp = n->op.substr(0, n->op.size() - 1);
                if (!binOpFromText(baseOp, bop))
                    throw std::runtime_error("Generacion IR (linea " + std::to_string(n->line) + "): operador de asignacion no soportado '" + n->op + "'.");
                // C++17 [expr.ass]: en E1 op= E2, E2 se evalua antes que E1; el valor
                // previo de la variable se lee despues de los efectos del lado derecho.
                Operand value = expression(n->a.at(1));
                IRInstr& i = emit(IROp::Binary, n->line);
                i.dst = newTemp(); i.a = tgt; i.b = value; i.text = baseOp;
                Operand computed = i.dst;
                emitMove(tgt, computed, n->line);
                return tgt;
            }
            case NK::IncDec: {
                if (n->op != "++" && n->op != "--")
                    throw std::runtime_error("Generacion IR (linea " + std::to_string(n->line) + "): operador no soportado '" + n->op + "'.");
                const NP& target = n->a.at(0);
                if (target->k != NK::Var) throw std::runtime_error("Generacion IR: ++/-- requiere una variable.");
                IRInstr& i = emit(IROp::IncDec, n->line);
                i.dst = newTemp(); i.a = Operand::var(resolve(target->name, target->line));
                i.text = n->op; i.flag = n->flag;
                return i.dst;
            }
            case NK::Call: {
                vector<Operand> args;
                for (size_t k = 0; k < n->a.size(); ++k) {
                    Operand o = expression(n->a[k]);
                    if (o.kind == Operand::Var) {
                        bool later = false;
                        for (size_t j = k + 1; j < n->a.size(); ++j) later = later || hasSideEffect(n->a[j]);
                        if (later) o = snapshot(o, n->line);
                    }
                    args.push_back(o);
                }
                IRInstr& i = emit(IROp::Call, n->line);
                i.dst = newTemp(); i.text = n->name; i.args = args;
                return i.dst;
            }
            case NK::MethodCall:
                throw std::runtime_error("Generacion IR (linea " + std::to_string(n->line) + "): las llamadas a metodos miembro no estan implementadas.");
            case NK::Ternary: {
                Operand dst = newTemp();
                const string noLabel = newLabel("ternary_no"), endLabel = newLabel("ternary_end");
                emitJump(IROp::JumpFalse, noLabel, expression(n->a.at(0)), n->line);
                emitMove(dst, expression(n->a.at(1)), n->line);
                emitJump(IROp::Jump, endLabel, Operand(), n->line);
                emitLabel(noLabel, n->line);
                emitMove(dst, expression(n->a.at(2)), n->line);
                emitLabel(endLabel, n->line);
                return dst;
            }
            default:
                throw std::runtime_error(string("Generacion IR: expresion no soportada: ") + nkName(n->k) + ".");
        }
    }

    void statement(const NP& n) {
        if (!n) return;
        switch (n->k) {
            case NK::Block:
                if (!n->flag) scopes.emplace_back();
                for (const auto& child : n->a) statement(child);
                if (!n->flag) scopes.pop_back();
                break;
            case NK::VarDecl: {
                if (typeFromName(n->type) == T_NONE || typeFromName(n->type) == T_VOID)
                    throw std::runtime_error("Generacion IR (linea " + std::to_string(n->line) + "): tipo no soportado '" + n->type + "'.");
                Operand init;
                if (!n->a.empty()) init = expression(n->a[0]);   // el inicializador no ve la variable que declara
                const string unique = declareName(n->name);
                IRInstr& d = emit(IROp::Declare, n->line);
                d.dst = Operand::var(unique); d.text = n->type;
                if (!n->a.empty()) emitMove(Operand::var(unique), init, n->line);
                else if (n->type == "std::string") emitMove(Operand::var(unique), Operand::constant(Value::Str("")), n->line);
                break;
            }
            case NK::ExprStmt:
                (void)expression(n->a.at(0));
                break;
            case NK::StreamOutput:
                for (const auto& value : n->a) {
                    Operand o = expression(value);
                    emit(IROp::Print, value->line).a = o;
                }
                if (n->flag) emit(IROp::PrintLn, n->line);
                break;
            case NK::If: {
                const string elseLabel = newLabel("if_else"), endLabel = newLabel("if_end");
                emitJump(IROp::JumpFalse, elseLabel, expression(n->a.at(0)), n->line);
                statement(n->a.at(1));
                emitJump(IROp::Jump, endLabel, Operand(), n->line);
                emitLabel(elseLabel, n->line);
                if (n->a.size() > 2) statement(n->a[2]);
                emitLabel(endLabel, n->line);
                break;
            }
            case NK::Switch: {
                if (n->a.empty()) throw std::runtime_error("Generacion IR: switch sin selector.");
                scopes.emplace_back();
                Operand selector = expression(n->a[0]);
                if (selector.kind == Operand::Var) selector = snapshot(selector, n->line);
                const string endLabel = newLabel("switch_end");
                vector<string> caseLabels;
                string defaultLabel = endLabel;
                for (const auto& c : n->b) {
                    const string caseLabel = newLabel(c->flag ? "switch_default" : "switch_case");
                    caseLabels.push_back(caseLabel);
                    if (c->flag) {
                        defaultLabel = caseLabel;
                    } else {
                        if (c->a.empty()) throw std::runtime_error("Generacion IR: case sin expresion.");
                        Operand value = expression(c->a[0]);
                        IRInstr& cmp = emit(IROp::Binary, c->line);
                        cmp.dst = newTemp(); cmp.a = selector; cmp.b = value; cmp.text = "==";
                        Operand eq = cmp.dst;
                        emitJump(IROp::JumpTrue, caseLabel, eq, c->line);
                    }
                }
                emitJump(IROp::Jump, defaultLabel, Operand(), n->line);
                // break sale del switch; continue conserva el destino del bucle exterior.
                const string continueTarget = loops.empty() ? string() : loops.back().continueLabel;
                loops.push_back({endLabel, continueTarget});
                for (size_t i = 0; i < n->b.size(); ++i) {
                    const NP& c = n->b[i];
                    emitLabel(caseLabels[i], c->line);
                    const size_t firstBody = c->flag ? 0 : 1;
                    for (size_t j = firstBody; j < c->a.size(); ++j) statement(c->a[j]);
                }
                loops.pop_back();
                emitLabel(endLabel, n->line);
                scopes.pop_back();
                break;
            }
            case NK::While: {
                const string start = newLabel("while_start"), end = newLabel("while_end");
                emitLabel(start, n->line);
                emitJump(IROp::JumpFalse, end, expression(n->a.at(0)), n->line);
                loops.push_back({end, start});
                statement(n->a.at(1));
                loops.pop_back();
                emitJump(IROp::Jump, start, Operand(), n->line);
                emitLabel(end, n->line);
                break;
            }
            case NK::DoWhile: {
                const string start = newLabel("do_start"), condLabel = newLabel("do_cond"), end = newLabel("do_end");
                emitLabel(start, n->line);
                loops.push_back({end, condLabel});
                statement(n->a.at(0));
                loops.pop_back();
                emitLabel(condLabel, n->line);
                emitJump(IROp::JumpTrue, start, expression(n->a.at(1)), n->line);
                emitLabel(end, n->line);
                break;
            }
            case NK::For: {
                scopes.emplace_back();
                statement(n->a.at(0));
                const string start = newLabel("for_start"), update = newLabel("for_update"), end = newLabel("for_end");
                emitLabel(start, n->line);
                if (n->a.at(1)) emitJump(IROp::JumpFalse, end, expression(n->a.at(1)), n->line);
                loops.push_back({end, update});
                statement(n->a.at(3));
                loops.pop_back();
                emitLabel(update, n->line);
                if (n->a.at(2)) statement(n->a.at(2));
                emitJump(IROp::Jump, start, Operand(), n->line);
                emitLabel(end, n->line);
                scopes.pop_back();
                break;
            }
            case NK::Return: {
                Operand v;
                if (!n->a.empty()) v = expression(n->a[0]);
                emit(IROp::Return, n->line).a = v;
                break;
            }
            case NK::Break:
                if (loops.empty()) throw std::runtime_error("Generacion IR: break fuera de un bucle.");
                emitJump(IROp::Jump, loops.back().breakLabel, Operand(), n->line);
                break;
            case NK::Continue:
                if (loops.empty() || loops.back().continueLabel.empty())
                    throw std::runtime_error("Generacion IR: continue fuera de un bucle.");
                emitJump(IROp::Jump, loops.back().continueLabel, Operand(), n->line);
                break;
            default:
                throw std::runtime_error(string("Generacion IR: sentencia no soportada: ") + nkName(n->k) + ".");
        }
    }

public:
    IRProgram generate(const NP& ast) {
        IRProgram result;
        if (!ast || ast->k != NK::Program) throw std::runtime_error("Generacion IR: AST invalido.");
        for (const auto& unit : ast->a) {
            for (const auto& method : unit->a) {
                if (!method || method->k != NK::Method)
                    throw std::runtime_error("Generacion IR: se encontro una funcion o miembro no soportado.");
                result.functions.emplace_back();
                fn = &result.functions.back();
                fn->name = method->name; fn->ret = method->type; fn->line = method->line;
                tempCounter = labelCounter = 0;
                loops.clear(); scopes.clear(); usedNames.clear();
                scopes.emplace_back();
                for (const auto& param : method->b) {
                    if (typeFromName(param->type) == T_NONE || typeFromName(param->type) == T_VOID)
                        throw std::runtime_error("Generacion IR (linea " + std::to_string(param->line) + "): tipo de parametro no soportado '" + param->type + "'.");
                    fn->params.push_back({param->type, declareName(param->name)});
                }
                statement(method->a.at(0));
            }
        }
        fn = nullptr;
        return result;
    }
};

static string formatInstr(const IRInstr& i) {
    std::ostringstream out;
    if (i.op != IROp::Label) out << "    ";
    out << std::left << std::setw(11) << (i.op == IROp::Label ? "" : irOpName(i.op));
    switch (i.op) {
        case IROp::Label: out.str(""); out << i.label << ":"; break;
        case IROp::Declare: out << i.text << " " << operandText(i.dst); break;
        case IROp::Move: out << operandText(i.dst) << " <- " << operandText(i.a); break;
        case IROp::Binary: out << operandText(i.dst) << " <- " << operandText(i.a) << " " << i.text << " " << operandText(i.b); break;
        case IROp::Unary: out << operandText(i.dst) << " <- " << i.text << " " << operandText(i.a); break;
        case IROp::IncDec:
            if (!i.dst.isNone()) out << operandText(i.dst) << " <- ";
            out << (i.flag ? i.text : "") << operandText(i.a) << (i.flag ? "" : i.text);
            break;
        case IROp::Cast: out << operandText(i.dst) << " <- (" << i.text << ") " << operandText(i.a); break;
        case IROp::Call:
            if (!i.dst.isNone()) out << operandText(i.dst) << " <- ";
            out << i.text << "(";
            for (size_t k = 0; k < i.args.size(); ++k) out << (k ? ", " : "") << operandText(i.args[k]);
            out << ")";
            break;
        case IROp::Return: if (!i.a.isNone()) out << operandText(i.a); break;
        case IROp::Print: out << operandText(i.a); break;
        case IROp::PrintLn: break;
        case IROp::Jump: out << i.label; break;
        case IROp::JumpFalse:
        case IROp::JumpTrue: out << operandText(i.a) << " -> " << i.label; break;
        case IROp::Nop: out << "(eliminado)"; break;
    }
    string s = out.str();
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (i.op != IROp::Label) {
        if (s.size() < 44) s += string(44 - s.size(), ' ');
        s += "  ; linea " + std::to_string(i.line);
    }
    return s;
}

static string formatIR(const IRProgram& program) {
    std::ostringstream out;
    out << "; Codigo intermedio de tres direcciones (TAC)\n";
    for (const auto& f : program.functions) {
        out << "\nfuncion " << f.ret << " " << f.name << "(";
        for (size_t k = 0; k < f.params.size(); ++k)
            out << (k ? ", " : "") << f.params[k].first << " " << f.params[k].second;
        out << ")  ; " << f.code.size() << " instrucciones\n";
        for (const auto& i : f.code) out << formatInstr(i) << "\n";
        out << "fin " << f.name << "\n";
    }
    return out.str();
}
