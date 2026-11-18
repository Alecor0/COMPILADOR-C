#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdint>
#include <cstdio>

using std::string;
using std::vector;
using std::to_string;

struct Value {
    enum T { V_VOID, INT, DOUBLE, BOOL, STR, ARR, NUL, EXC } t = NUL;
    int32_t i = 0;
    double d = 0;
    bool b = false;
    string s;
    std::shared_ptr<vector<Value>> arr;
    string cls;

    static Value Int(long long v) { Value x; x.t = INT; x.i = (int32_t)(uint32_t)(uint64_t)v; return x; }
    static Value Dbl(double v) { Value x; x.t = DOUBLE; x.d = v; return x; }
    static Value Bool(bool v) { Value x; x.t = BOOL; x.b = v; return x; }
    static Value Str(const string& v) { Value x; x.t = STR; x.s = v; return x; }
    static Value Null() { return Value(); }
    static Value Void() { Value x; x.t = V_VOID; return x; }
};

static string fmtDouble(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    if (d == 0) return std::signbit(d) ? "-0.0" : "0.0";
    char buf[128];
    snprintf(buf, sizeof buf, "%.16g", d);
    string s = buf;
    if (s.find('.') == string::npos && s.find('e') == string::npos && s.find('E') == string::npos) {
        s += ".0";
    }
    return s;
}

static string excToString(const Value& e) {
    string n = e.cls.empty() ? "exception" : e.cls;
    if (!e.s.empty()) n += ": " + e.s;
    return n;
}

static string toStr(const Value& v) {
    switch (v.t) {
        case Value::INT: return to_string(v.i);
        case Value::DOUBLE: return fmtDouble(v.d);
        case Value::BOOL: return v.b ? "true" : "false";
        case Value::STR: return v.s;
        case Value::NUL: return "null";
        case Value::ARR: return "[arreglo@" + to_string(v.arr ? v.arr->size() : 0) + "]";
        case Value::EXC: return excToString(v);
        default: return "";
    }
}

enum class NK {
    Program, Class, Field, Method, Block, VarDecl, ExprStmt, If, While,
    DoWhile, For, ForEach, Return, Break, Continue, Try, Catch, Throw,
    Lit, Var, Binary, Unary, Assign, IncDec, Call, MethodCall, Member,
    Index, NewArray, ArrayInit, NewObj, Ternary, Cast, Switch, Case, StreamOutput
};

static const char* nkName(NK k) {
    static const char* const names[] = {
        "Program", "Class", "Field", "Method", "Block", "VarDecl", "ExprStmt", "If", "While",
        "DoWhile", "For", "ForEach", "Return", "Break", "Continue", "Try", "Catch", "Throw",
        "Lit", "Var", "Binary", "Unary", "Assign", "IncDec", "Call", "MethodCall", "Member",
        "Index", "NewArray", "ArrayInit", "NewObj", "Ternary", "Cast", "Switch", "Case", "StreamOutput"
    };
    static_assert(sizeof(names) / sizeof(names[0]) == (size_t)NK::StreamOutput + 1, "nkName desincronizado con NK");
    return names[(size_t)k];
}

struct Node;
using NP = std::shared_ptr<Node>;

struct Node {
    NK k;
    string name, type, op;
    Value val;
    vector<NP> a, b;
    bool flag = false;
    int line = 0;

    Node(NK kind, int ln) : k(kind), line(ln) {}
    static NP mk(NK k, int line) { return std::make_shared<Node>(k, line); }
    static NP mkLit(const Value& v, int line) { auto n = mk(NK::Lit, line); n->val = v; return n; }
};

// Nombre interno de la unidad que agrupa las funciones globales. No es un
// identificador valido de C++, asi que ninguna clase del usuario puede llamarse asi.
static const char* const GLOBAL_UNIT_NAME = "<global>";
