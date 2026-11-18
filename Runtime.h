#pragma once
// Semantica de valores compartida por el optimizador (plegado de constantes)
// y por la maquina virtual CVM17. Un unico lugar define como se comporta cada
// operacion, asi el codigo optimizado y el no optimizado nunca divergen.
#include "AST.h"
#include <climits>
#include <cmath>
#include <stdexcept>

enum class BinOp : uint8_t { Add = 1, Sub, Mul, Div, Mod, Lt, Gt, Le, Ge, Eq, Ne, BitAnd, BitOr, BitXor, Shl, Shr };
enum class UnOp : uint8_t { Neg = 1, Pos, Not, BitNot };
enum TypeCode : uint8_t { T_NONE = 0, T_INT = 1, T_DOUBLE, T_BOOL, T_STRING, T_VOID };

static const char* binOpText(BinOp op) {
    static const char* const t[] = {"", "+", "-", "*", "/", "%", "<", ">", "<=", ">=", "==", "!=", "&", "|", "^", "<<", ">>"};
    return t[static_cast<size_t>(op)];
}
static bool binOpFromText(const string& s, BinOp& op) {
    for (uint8_t i = 1; i <= static_cast<uint8_t>(BinOp::Shr); ++i)
        if (s == binOpText(static_cast<BinOp>(i))) { op = static_cast<BinOp>(i); return true; }
    return false;
}
static const char* unOpText(UnOp op) {
    static const char* const t[] = {"", "-", "+", "!", "~"};
    return t[static_cast<size_t>(op)];
}
static bool unOpFromText(const string& s, UnOp& op) {
    for (uint8_t i = 1; i <= static_cast<uint8_t>(UnOp::BitNot); ++i)
        if (s == unOpText(static_cast<UnOp>(i))) { op = static_cast<UnOp>(i); return true; }
    return false;
}
static TypeCode typeFromName(const string& n) {
    if (n == "int") return T_INT;
    if (n == "double") return T_DOUBLE;
    if (n == "bool") return T_BOOL;
    if (n == "std::string") return T_STRING;
    if (n == "void") return T_VOID;
    return T_NONE;
}
static const char* typeText(uint8_t t) {
    switch (t) {
        case T_INT: return "int";
        case T_DOUBLE: return "double";
        case T_BOOL: return "bool";
        case T_STRING: return "std::string";
        case T_VOID: return "void";
        default: return "?";
    }
}

[[noreturn]] static void rtError(int line, const string& m) {
    throw std::runtime_error("Error de ejecucion (linea " + to_string(line) + "): " + m);
}

static string valueTypeName(const Value& v) {
    switch (v.t) {
        case Value::INT: return "int";
        case Value::DOUBLE: return "double";
        case Value::BOOL: return "bool";
        case Value::STR: return "std::string";
        case Value::NUL: return "null";
        case Value::V_VOID: return "void";
        default: return "desconocido";
    }
}
static bool isNumV(const Value& v) { return v.t == Value::INT || v.t == Value::DOUBLE || v.t == Value::BOOL; }
static double toD(const Value& v) {
    if (v.t == Value::INT) return static_cast<double>(v.i);
    if (v.t == Value::BOOL) return v.b ? 1.0 : 0.0;
    return v.d;
}
static long long toI(const Value& v) {
    if (v.t == Value::BOOL) return v.b ? 1LL : 0LL;
    if (v.t == Value::DOUBLE) return static_cast<long long>(v.d);
    return v.i;
}
static int32_t checkedInt(int line, long long value) {
    if (value < INT_MIN || value > INT_MAX)
        rtError(line, "desbordamiento de int; el resultado no es representable en C++.");
    return static_cast<int32_t>(value);
}
static int32_t checkedDoubleToInt(int line, double value) {
    if (!std::isfinite(value) || value < static_cast<double>(INT_MIN) || value > static_cast<double>(INT_MAX))
        rtError(line, "conversion de double a int fuera del rango representable.");
    return static_cast<int32_t>(value); // truncamiento hacia cero
}

static Value coerceTo(int line, uint8_t type, const Value& v) {
    if (type == T_INT && v.t == Value::DOUBLE) return Value::Int(checkedDoubleToInt(line, v.d));
    if (type == T_INT && isNumV(v)) return Value::Int(toI(v));
    if (type == T_DOUBLE && isNumV(v)) return Value::Dbl(toD(v));
    if (type == T_BOOL && isNumV(v)) return Value::Bool(toD(v) != 0.0);
    if (type == T_STRING && v.t == Value::STR) return v;
    rtError(line, "no se puede asignar un valor de tipo '" + valueTypeName(v) + "' a '" + typeText(type) + "'.");
}

static Value binaryOp(int line, BinOp op, const Value& l, const Value& r) {
    const string opText = binOpText(op);
    if (op == BinOp::Add && (l.t == Value::STR || r.t == Value::STR)) {
        if (l.t == Value::V_VOID || r.t == Value::V_VOID) rtError(line, "no se puede concatenar 'void'.");
        return Value::Str(toStr(l) + toStr(r));
    }
    if (op == BinOp::Eq || op == BinOp::Ne) {
        bool eq;
        if (isNumV(l) && isNumV(r)) eq = toD(l) == toD(r);
        else if (l.t == Value::STR && r.t == Value::STR) eq = l.s == r.s;
        else rtError(line, "'" + opText + "' no se puede aplicar a '" + valueTypeName(l) + "' y '" + valueTypeName(r) + "'.");
        return Value::Bool(op == BinOp::Eq ? eq : !eq);
    }
    if (!isNumV(l) || !isNumV(r))
        rtError(line, "el operador '" + opText + "' no se puede aplicar a '" + valueTypeName(l) + "' y '" + valueTypeName(r) + "'.");

    if (l.t != Value::DOUBLE && r.t != Value::DOUBLE) {
        const long long a = toI(l), b = toI(r);
        switch (op) {
            case BinOp::Add: return Value::Int(checkedInt(line, a + b));
            case BinOp::Sub: return Value::Int(checkedInt(line, a - b));
            case BinOp::Mul: return Value::Int(checkedInt(line, a * b));
            case BinOp::Div:
                if (b == 0) rtError(line, "division entera entre cero.");
                if (a == INT_MIN && b == -1) rtError(line, "desbordamiento de int en division.");
                return Value::Int(checkedInt(line, a / b));
            case BinOp::Mod:
                if (b == 0) rtError(line, "modulo entero entre cero.");
                if (a == INT_MIN && b == -1) rtError(line, "desbordamiento de int en modulo.");
                return Value::Int(checkedInt(line, a % b));
            case BinOp::Lt: return Value::Bool(a < b);
            case BinOp::Gt: return Value::Bool(a > b);
            case BinOp::Le: return Value::Bool(a <= b);
            case BinOp::Ge: return Value::Bool(a >= b);
            case BinOp::BitAnd: return Value::Int(a & b);
            case BinOp::BitOr: return Value::Int(a | b);
            case BinOp::BitXor: return Value::Int(a ^ b);
            case BinOp::Shl:
            case BinOp::Shr: {
                if (b < 0 || b >= 32) rtError(line, "cantidad de desplazamiento fuera del rango 0..31.");
                if (op == BinOp::Shl) {
                    if (a < 0) rtError(line, "desplazamiento a la izquierda de un int negativo no es valido en C++17.");
                    return Value::Int(checkedInt(line, a * (1LL << static_cast<unsigned>(b))));
                }
                // Para int negativo se usa desplazamiento aritmetico (comportamiento comun de GCC/Clang).
                const uint32_t ua = static_cast<uint32_t>(static_cast<int32_t>(a));
                if (a >= 0) return Value::Int(static_cast<int32_t>(ua >> static_cast<unsigned>(b)));
                uint32_t shifted = ua >> static_cast<unsigned>(b);
                if (b != 0) shifted |= (~uint32_t(0) << (32U - static_cast<unsigned>(b)));
                return Value::Int(static_cast<int32_t>(shifted));
            }
            default: break;
        }
    } else {
        const double a = toD(l), b = toD(r);
        switch (op) {
            case BinOp::Add: return Value::Dbl(a + b);
            case BinOp::Sub: return Value::Dbl(a - b);
            case BinOp::Mul: return Value::Dbl(a * b);
            case BinOp::Div: return Value::Dbl(a / b);
            case BinOp::Mod: return Value::Dbl(std::fmod(a, b));
            case BinOp::Lt: return Value::Bool(a < b);
            case BinOp::Gt: return Value::Bool(a > b);
            case BinOp::Le: return Value::Bool(a <= b);
            case BinOp::Ge: return Value::Bool(a >= b);
            default: break;
        }
    }
    rtError(line, "operador no soportado: '" + opText + "'.");
}

static Value unaryOp(int line, UnOp op, const Value& v) {
    if (op == UnOp::Not && isNumV(v)) return Value::Bool(toD(v) == 0.0);
    if (op == UnOp::Pos && v.t == Value::BOOL) return Value::Int(toI(v));
    if (op == UnOp::Pos && isNumV(v)) return v;
    if (op == UnOp::Neg && (v.t == Value::INT || v.t == Value::BOOL)) return Value::Int(checkedInt(line, -toI(v)));
    if (op == UnOp::Neg && v.t == Value::DOUBLE) return Value::Dbl(-v.d);
    if (op == UnOp::BitNot && (v.t == Value::INT || v.t == Value::BOOL)) return Value::Int(~toI(v));
    rtError(line, string("el operador unario '") + unOpText(op) + "' no se puede aplicar a '" + valueTypeName(v) + "'.");
}

static Value castOp(int line, uint8_t type, const Value& v) {
    if (type == T_INT && v.t == Value::INT) return v;
    if (type == T_INT && v.t == Value::DOUBLE) return Value::Int(checkedDoubleToInt(line, v.d));
    if (type == T_DOUBLE && isNumV(v)) return Value::Dbl(toD(v));
    rtError(line, "conversion no soportada de '" + valueTypeName(v) + "' a '" + typeText(type) + "'.");
}

// Resultado de ++/-- sobre un valor; devuelve el nuevo valor.
static Value incDecValue(int line, const Value& old, bool decrement) {
    const int delta = decrement ? -1 : 1;
    if (old.t == Value::INT) return Value::Int(checkedInt(line, static_cast<long long>(old.i) + delta));
    if (old.t == Value::DOUBLE) return Value::Dbl(old.d + delta);
    rtError(line, string("'") + (decrement ? "--" : "++") + "' no se puede aplicar a '" + valueTypeName(old) + "'.");
}

// Condicion de if/while/for/&&/||/?: debe ser escalar.
static bool truthValue(int line, const Value& v) {
    if (!isNumV(v)) rtError(line, "la condicion debe ser escalar (bool, int o double), no '" + valueTypeName(v) + "'.");
    return toD(v) != 0.0;
}
