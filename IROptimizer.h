#pragma once
// Fase 5: optimizacion del codigo intermedio.
// Pasadas (se repiten hasta que ya no hay cambios):
//   1. Propagacion de constantes y copias dentro de cada bloque basico.
//   2. Plegado de constantes (usa la misma semantica que la VM; si una operacion
//      fallaria en ejecucion, NO se pliega para que el error siga ocurriendo al ejecutar).
//   3. Simplificaciones algebraicas (x+0, x*1, ...) sobre enteros.
//   4. Saltos condicionales con condicion constante.
//   5. Codigo inalcanzable, encadenamiento de saltos, saltos al destino siguiente y etiquetas sin uso.
//   6. Codigo muerto: temporales y variables que nunca se leen.
#include "IntermediateCode.h"
#include <map>
#include <set>
#include <algorithm>

struct OptStats {
    int propagated = 0;     // usos reemplazados por constantes
    int copies = 0;         // usos reemplazados por copias
    int folded = 0;         // operaciones evaluadas en compilacion
    int algebraic = 0;      // simplificaciones algebraicas
    int branches = 0;       // saltos condicionales resueltos
    int unreachable = 0;    // instrucciones inalcanzables eliminadas
    int jumps = 0;          // saltos encadenados o redundantes eliminados
    int labels = 0;         // etiquetas sin uso eliminadas
    int deadCode = 0;       // definiciones muertas eliminadas
    int rounds = 0;
    size_t before = 0, after = 0;
};

class IROptimizer {
private:
    OptStats stats;

    // ----------------------------------------------------------------- utilidades
    static bool sameOperand(const Operand& x, const Operand& y) {
        return x.kind == y.kind && x.name == y.name;
    }

    static vector<Operand*> readOperands(IRInstr& i) {
        vector<Operand*> r;
        switch (i.op) {
            case IROp::Move: case IROp::Unary: case IROp::Cast: case IROp::Return:
            case IROp::Print: case IROp::JumpFalse: case IROp::JumpTrue:
                r.push_back(&i.a); break;
            case IROp::Binary: r.push_back(&i.a); r.push_back(&i.b); break;
            case IROp::Call: for (auto& a : i.args) r.push_back(&a); break;
            default: break;
        }
        return r;
    }

    static void addUses(const IRInstr& i, std::map<string, int>& uses) {
        auto use = [&](const Operand& o) { if (o.isName()) ++uses[o.name]; };
        switch (i.op) {
            case IROp::Move: case IROp::Unary: case IROp::Cast: case IROp::Return:
            case IROp::Print: case IROp::JumpFalse: case IROp::JumpTrue: use(i.a); break;
            case IROp::Binary: use(i.a); use(i.b); break;
            case IROp::IncDec: use(i.a); break;      // ++/-- lee y escribe la variable
            case IROp::Call: for (const auto& a : i.args) use(a); break;
            default: break;
        }
    }

    // Tipos declarados de las variables (nombres unicos, no cambian dentro de la funcion).
    static std::map<string, uint8_t> declaredTypes(const IRFunction& f) {
        std::map<string, uint8_t> t;
        for (const auto& p : f.params) t[p.second] = typeFromName(p.first);
        for (const auto& i : f.code) if (i.op == IROp::Declare) t[i.dst.name] = typeFromName(i.text);
        return t;
    }

    // ------------------------------------------------ 1-4: propagacion y plegado
    bool propagateAndFold(IRFunction& f) {
        bool changed = false;
        const auto types = declaredTypes(f);
        std::map<string, Operand> known;      // nombre -> constante o copia valida en este bloque

        auto invalidate = [&](const Operand& o) {
            if (!o.isName()) return;
            known.erase(o.name);
            for (auto it = known.begin(); it != known.end();) {
                if (it->second.isName() && it->second.name == o.name) it = known.erase(it);
                else ++it;
            }
        };
        auto intVar = [&](const Operand& o) {
            if (o.kind != Operand::Var) return false;
            auto t = types.find(o.name);
            return t != types.end() && t->second == T_INT;
        };
        auto intConst = [](const Operand& o, int v) {
            return o.isConst() && o.val.t == Value::INT && o.val.i == v;
        };

        for (auto& i : f.code) {
            if (i.op == IROp::Label) { known.clear(); continue; }

            // sustitucion de lecturas
            for (Operand* o : readOperands(i)) {
                if (!o->isName()) continue;
                auto k = known.find(o->name);
                if (k == known.end()) continue;
                if (k->second.isConst()) ++stats.propagated; else ++stats.copies;
                *o = k->second;
                changed = true;
            }

            // plegado
            if (i.op == IROp::Binary) {
                BinOp bop;
                if (binOpFromText(i.text, bop) && i.a.isConst() && i.b.isConst()) {
                    try {
                        Value v = binaryOp(i.line, bop, i.a.val, i.b.val);
                        i.op = IROp::Move; i.a = Operand::constant(v); i.b = Operand(); i.text.clear();
                        ++stats.folded; changed = true;
                    } catch (const std::runtime_error&) { /* fallaria en ejecucion: se deja intacto */ }
                } else if (binOpFromText(i.text, bop)) {
                    // identidades enteras: el resultado coincide con la variable (tipo int)
                    Operand keep; bool simplify = false;
                    if (bop == BinOp::Add) {
                        if (intVar(i.a) && intConst(i.b, 0)) { keep = i.a; simplify = true; }
                        else if (intConst(i.a, 0) && intVar(i.b)) { keep = i.b; simplify = true; }
                    } else if (bop == BinOp::Sub) {
                        if (intVar(i.a) && intConst(i.b, 0)) { keep = i.a; simplify = true; }
                    } else if (bop == BinOp::Mul) {
                        if (intVar(i.a) && intConst(i.b, 1)) { keep = i.a; simplify = true; }
                        else if (intConst(i.a, 1) && intVar(i.b)) { keep = i.b; simplify = true; }
                    } else if (bop == BinOp::Div) {
                        if (intVar(i.a) && intConst(i.b, 1)) { keep = i.a; simplify = true; }
                    }
                    if (simplify) {
                        i.op = IROp::Move; i.a = keep; i.b = Operand(); i.text.clear();
                        ++stats.algebraic; changed = true;
                    }
                }
            } else if (i.op == IROp::Unary && i.a.isConst()) {
                UnOp uop;
                if (unOpFromText(i.text, uop)) {
                    try {
                        Value v = unaryOp(i.line, uop, i.a.val);
                        i.op = IROp::Move; i.a = Operand::constant(v); i.text.clear();
                        ++stats.folded; changed = true;
                    } catch (const std::runtime_error&) {}
                }
            } else if (i.op == IROp::Cast && i.a.isConst()) {
                try {
                    Value v = castOp(i.line, typeFromName(i.text), i.a.val);
                    i.op = IROp::Move; i.a = Operand::constant(v); i.text.clear();
                    ++stats.folded; changed = true;
                } catch (const std::runtime_error&) {}
            } else if ((i.op == IROp::JumpFalse || i.op == IROp::JumpTrue) && i.a.isConst() && isNumV(i.a.val)) {
                const bool value = toD(i.a.val) != 0.0;
                const bool take = (i.op == IROp::JumpFalse) ? !value : value;
                if (take) { i.op = IROp::Jump; i.a = Operand(); }
                else { i.op = IROp::Nop; i.a = Operand(); i.label.clear(); }
                ++stats.branches; changed = true;
            }

            // actualizacion del conocimiento
            switch (i.op) {
                case IROp::Declare: invalidate(i.dst); break;
                case IROp::Move: {
                    invalidate(i.dst);
                    if (i.dst.kind == Operand::Temp) {
                        if (!sameOperand(i.dst, i.a)) known[i.dst.name] = i.a;
                    } else if (i.a.isConst()) {
                        auto t = types.find(i.dst.name);
                        if (t != types.end()) {
                            try {
                                Value v = coerceTo(i.line, t->second, i.a.val);   // se convierte al tipo de la variable
                                if (!(v.t == i.a.val.t)) { i.a = Operand::constant(v); ++stats.folded; changed = true; }
                                else i.a = Operand::constant(v);
                                known[i.dst.name] = i.a;
                            } catch (const std::runtime_error&) {}
                        }
                    }
                    break;
                }
                case IROp::Binary: case IROp::Unary: case IROp::Cast: case IROp::Call:
                    invalidate(i.dst); break;
                case IROp::IncDec:
                    invalidate(i.dst); invalidate(i.a); break;
                case IROp::Jump: case IROp::JumpFalse: case IROp::JumpTrue: case IROp::Return:
                    known.clear(); break;
                default: break;
            }
        }
        return changed;
    }

    // ------------------------------------------------ 5: flujo de control
    static std::set<string> referencedLabels(const IRFunction& f) {
        std::set<string> s;
        for (const auto& i : f.code)
            if (i.op == IROp::Jump || i.op == IROp::JumpFalse || i.op == IROp::JumpTrue) s.insert(i.label);
        return s;
    }

    bool simplifyControlFlow(IRFunction& f) {
        bool changed = false;

        // (a) codigo inalcanzable: lo que sigue a un salto incondicional/return hasta una etiqueta usada
        {
            const auto refs = referencedLabels(f);
            vector<IRInstr> out;
            bool dead = false;
            for (auto& i : f.code) {
                if (i.op == IROp::Nop) { changed = true; continue; }
                if (i.op == IROp::Label) {
                    if (refs.count(i.label)) dead = false;
                    out.push_back(i);
                    continue;
                }
                // Un DECLARE inalcanzable se conserva: el ambito y el tipo de la variable
                // son estaticos en C++ (p. ej. 'case 0: int x; ... case 1: x = d;'), y tanto
                // el optimizador como la VM obtienen el tipo del slot de esa declaracion.
                if (dead && i.op == IROp::Declare) { out.push_back(i); continue; }
                if (dead) { ++stats.unreachable; changed = true; continue; }
                out.push_back(i);
                if (i.op == IROp::Jump || i.op == IROp::Return) dead = true;
            }
            f.code = std::move(out);
        }

        // (b) encadenamiento de saltos: Jump L donde L: Jump M  ->  Jump M
        {
            std::map<string, size_t> pos;
            for (size_t k = 0; k < f.code.size(); ++k) if (f.code[k].op == IROp::Label) pos[f.code[k].label] = k;
            auto finalTarget = [&](string label) {
                for (int hop = 0; hop < 32; ++hop) {
                    auto p = pos.find(label);
                    if (p == pos.end()) break;
                    size_t k = p->second;
                    while (k < f.code.size() && f.code[k].op == IROp::Label) ++k;
                    if (k < f.code.size() && f.code[k].op == IROp::Jump && f.code[k].label != label) label = f.code[k].label;
                    else break;
                }
                return label;
            };
            for (auto& i : f.code) {
                if (i.op == IROp::Jump || i.op == IROp::JumpFalse || i.op == IROp::JumpTrue) {
                    const string t = finalTarget(i.label);
                    if (t != i.label) { i.label = t; ++stats.jumps; changed = true; }
                }
            }
        }

        // (c) salto al destino inmediato: Jump L seguido (tras otras etiquetas) de L:
        {
            vector<IRInstr> out;
            for (size_t k = 0; k < f.code.size(); ++k) {
                const IRInstr& i = f.code[k];
                if (i.op == IROp::Jump) {
                    bool falls = false;
                    for (size_t j = k + 1; j < f.code.size() && f.code[j].op == IROp::Label; ++j)
                        if (f.code[j].label == i.label) { falls = true; break; }
                    if (falls) { ++stats.jumps; changed = true; continue; }
                }
                out.push_back(i);
            }
            f.code = std::move(out);
        }

        // (d) etiquetas sin ningun salto
        {
            const auto refs = referencedLabels(f);
            vector<IRInstr> out;
            for (auto& i : f.code) {
                if (i.op == IROp::Label && !refs.count(i.label)) { ++stats.labels; changed = true; continue; }
                out.push_back(i);
            }
            f.code = std::move(out);
        }
        return changed;
    }

    // ------------------------------------------------ 6: codigo muerto
    bool removeDeadCode(IRFunction& f) {
        bool changed = false;
        std::map<string, int> uses;
        for (const auto& i : f.code) addUses(i, uses);
        auto used = [&](const Operand& o) { auto u = uses.find(o.name); return u != uses.end() && u->second > 0; };
        const auto types = declaredTypes(f);

        vector<IRInstr> out;
        for (auto& i : f.code) {
            if (i.op == IROp::Move && i.dst.kind == Operand::Temp && !used(i.dst)) { ++stats.deadCode; changed = true; continue; }
            // variable que nunca se lee: se eliminan sus asignaciones de constante (no pueden fallar)
            if (i.op == IROp::Move && i.dst.kind == Operand::Var && !used(i.dst) && i.a.isConst()) {
                bool safe = true;
                auto t = types.find(i.dst.name);
                if (t == types.end()) safe = false;
                else { try { (void)coerceTo(i.line, t->second, i.a.val); } catch (const std::runtime_error&) { safe = false; } }
                if (safe) { ++stats.deadCode; changed = true; continue; }
            }
            if ((i.op == IROp::Call || i.op == IROp::IncDec) && i.dst.kind == Operand::Temp && !used(i.dst)) {
                i.dst = Operand(); changed = true;     // el resultado no se usa; el efecto se conserva
            }
            out.push_back(i);
        }
        f.code = std::move(out);

        // Declare de variables sin ninguna otra referencia
        std::map<string, int> refs;
        for (const auto& i : f.code) {
            if (i.op == IROp::Declare) continue;
            if (i.dst.isName()) ++refs[i.dst.name];
            for (const Operand* o : {&i.a, &i.b}) if (o->isName()) ++refs[o->name];
            for (const auto& a : i.args) if (a.isName()) ++refs[a.name];
        }
        vector<IRInstr> out2;
        for (auto& i : f.code) {
            if (i.op == IROp::Declare && refs[i.dst.name] == 0) { ++stats.deadCode; changed = true; continue; }
            out2.push_back(i);
        }
        f.code = std::move(out2);
        return changed;
    }

public:
    const OptStats& statistics() const { return stats; }

    IRProgram optimize(IRProgram program) {
        stats = OptStats();
        stats.before = program.instructionCount();
        for (auto& f : program.functions) {
            for (int round = 0; round < 12; ++round) {
                bool c = false;
                c = propagateAndFold(f) || c;
                c = simplifyControlFlow(f) || c;
                c = removeDeadCode(f) || c;
                ++stats.rounds;
                if (!c) break;
            }
        }
        stats.after = program.instructionCount();
        return program;
    }
};
