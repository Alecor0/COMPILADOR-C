#pragma once
// Maquina virtual CVM17: ejecuta el modulo binario producido por la fase 6.
// Usa una pila de marcos propia (sin recursion de C++), por lo que una recursion
// infinita produce un error controlado en lugar de un desbordamiento de pila.
#include "ObjectCodeGenerator.h"
#include <iostream>
#include <unordered_map>

class VirtualMachine {
private:
    static constexpr unsigned long long MAX_STEPS = 100000000ULL;
    static constexpr size_t MAX_DEPTH = 20000;

    struct Frame {
        const BCFunction* fn = nullptr;
        uint32_t pc = 0;
        vector<Value> slots;
        vector<uint8_t> init, type;
        int32_t retDst = NO_OPERAND;
        int callLine = 0;
    };

    const BCModule& mod;
    std::ostream& out;
    unsigned long long steps = 0;
    std::unordered_map<string, vector<const BCFunction*>> byName;

    static string baseName(const string& n) {
        const size_t dot = n.find('.');
        return dot == string::npos ? n : n.substr(0, dot);
    }

    const Value& read(const Frame& f, int32_t o, int line) const {
        if (o < 0) return mod.consts[static_cast<size_t>(-(static_cast<int64_t>(o)) - 1)];
        if (!f.init[o])
            rtError(line, "la variable '" + baseName(f.fn->slotNames[o]) + "' puede no haber sido inicializada.");
        return f.slots[o];
    }
    static void write(Frame& f, int32_t dst, Value v) {
        if (dst == NO_OPERAND) return;
        f.slots[dst] = std::move(v);
        f.init[dst] = 1;
    }

    // Resolucion de sobrecargas segun los tipos reales de los argumentos.
    const BCFunction* resolve(const string& name, const vector<Value>& args, int line) const {
        auto it = byName.find(name);
        const BCFunction* chosen = nullptr;
        int best = -1; bool ambiguous = false;
        if (it != byName.end()) {
            for (const BCFunction* fn : it->second) {
                if (fn->params.size() != args.size()) continue;
                int score = 0; bool viable = true;
                for (size_t k = 0; k < args.size(); ++k) {
                    const uint8_t target = fn->params[k];
                    const Value& v = args[k];
                    const bool can = (target == T_STRING && v.t == Value::STR) ||
                                     ((target == T_INT || target == T_DOUBLE || target == T_BOOL) && isNumV(v));
                    if (!can) { viable = false; break; }
                    const bool exact = (target == T_INT && v.t == Value::INT) || (target == T_DOUBLE && v.t == Value::DOUBLE) ||
                                       (target == T_BOOL && v.t == Value::BOOL) || (target == T_STRING && v.t == Value::STR);
                    score += exact ? 4 : 1;
                }
                if (!viable) continue;
                if (score > best) { chosen = fn; best = score; ambiguous = false; }
                else if (score == best) ambiguous = true;
            }
        }
        if (!chosen) rtError(line, "no existe una sobrecarga ejecutable de '" + name + "'.");
        if (ambiguous) rtError(line, "llamada ambigua a la funcion '" + name + "'.");
        return chosen;
    }

    static Frame makeFrame(const BCFunction* fn) {
        Frame f; f.fn = fn; f.pc = fn->entry;
        f.slots.resize(fn->nslots); f.init.assign(fn->nslots, 0); f.type.assign(fn->nslots, T_NONE);
        return f;
    }

public:
    VirtualMachine(const BCModule& m, std::ostream& o) : mod(m), out(o) {
        for (const auto& f : mod.funcs) if (f.name != "main") byName[f.name].push_back(&f);
    }
    unsigned long long stepsExecuted() const { return steps; }

    void run() {
        const BCFunction* mainFn = nullptr;
        for (const auto& f : mod.funcs) if (f.name == "main") mainFn = &f;
        if (!mainFn) throw std::runtime_error("Error de ejecucion: no existe la funcion 'main'.");
        steps = 0;
        vector<Frame> stack;
        stack.push_back(makeFrame(mainFn));

        for (;;) {
            Frame& f = stack.back();
            const BCInstr& in = mod.code[f.pc++];
            if (++steps > MAX_STEPS) rtError(in.line, "limite de ejecucion excedido (posible bucle infinito).");
            switch (static_cast<Opc>(in.op)) {
                case Opc::Declare:
                    f.type[in.dst] = in.sub; f.init[in.dst] = 0;
                    break;
                case Opc::Move: {
                    Value v = read(f, in.a, in.line);
                    if (f.type[in.dst] != T_NONE) v = coerceTo(in.line, f.type[in.dst], v);
                    write(f, in.dst, std::move(v));
                    break;
                }
                case Opc::Binary:
                    write(f, in.dst, binaryOp(in.line, static_cast<BinOp>(in.sub), read(f, in.a, in.line), read(f, in.b, in.line)));
                    break;
                case Opc::Unary:
                    write(f, in.dst, unaryOp(in.line, static_cast<UnOp>(in.sub), read(f, in.a, in.line)));
                    break;
                case Opc::IncDec: {
                    const Value old = read(f, in.a, in.line);
                    Value updated = incDecValue(in.line, old, (in.sub & 1) != 0);
                    f.slots[in.a] = updated;
                    write(f, in.dst, (in.sub & 2) ? updated : old);
                    break;
                }
                case Opc::Cast:
                    write(f, in.dst, castOp(in.line, in.sub, read(f, in.a, in.line)));
                    break;
                case Opc::Print: {
                    const Value& v = read(f, in.a, in.line);
                    if (v.t == Value::V_VOID) rtError(in.line, "cout no puede insertar una expresion void.");
                    if (v.t == Value::BOOL) out << (v.b ? 1 : 0);
                    else out << toStr(v);
                    break;
                }
                case Opc::PrintLn:
                    out << '\n'; out.flush();
                    break;
                case Opc::Jmp:
                    f.pc = static_cast<uint32_t>(in.dst);
                    break;
                case Opc::JmpF:
                    if (!truthValue(in.line, read(f, in.a, in.line))) f.pc = static_cast<uint32_t>(in.dst);
                    break;
                case Opc::JmpT:
                    if (truthValue(in.line, read(f, in.a, in.line))) f.pc = static_cast<uint32_t>(in.dst);
                    break;
                case Opc::Call: {
                    vector<Value> args;
                    args.reserve(in.args.size());
                    for (int32_t a : in.args) args.push_back(read(f, a, in.line));
                    const string& name = mod.consts[static_cast<size_t>(-(static_cast<int64_t>(in.a)) - 1)].s;
                    const BCFunction* callee = resolve(name, args, in.line);
                    if (stack.size() >= MAX_DEPTH)
                        rtError(in.line, "limite de recursion excedido (posible recursion infinita en '" + name + "').");
                    Frame nf = makeFrame(callee);
                    nf.retDst = in.dst; nf.callLine = in.line;
                    for (size_t k = 0; k < args.size(); ++k) {
                        nf.type[k] = callee->params[k];
                        nf.slots[k] = coerceTo(in.line, callee->params[k], args[k]);
                        nf.init[k] = 1;
                    }
                    stack.push_back(std::move(nf));
                    break;
                }
                case Opc::Ret:
                case Opc::FnEnd: {
                    const bool explicitReturn = static_cast<Opc>(in.op) == Opc::Ret;
                    if (stack.size() == 1) return;          // main termina (C++ devuelve 0 implicitamente)
                    const BCFunction* fn = f.fn;
                    const int callLine = f.callLine;
                    const int32_t retDst = f.retDst;
                    Value result = Value::Void();
                    if (explicitReturn && in.a != NO_OPERAND) result = read(f, in.a, in.line);
                    else if (fn->ret != T_VOID)
                        rtError(callLine, "la funcion '" + fn->name + "' termino sin devolver un valor.");
                    if (fn->ret == T_VOID) result = Value::Void();
                    else result = coerceTo(callLine, fn->ret, result);
                    stack.pop_back();
                    write(stack.back(), retDst, std::move(result));
                    break;
                }
            }
        }
    }
};
