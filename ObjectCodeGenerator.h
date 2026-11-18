#pragma once
// Fase 6: generacion del modulo objeto binario CVM17 (version 2).
//
//   cabecera   "CVM17OBJ" | u16 version=2 | u16 flags=0
//   constantes u32 n  { u8 tag (1 int32, 2 double, 3 bool, 4 string, 5 null) + datos }
//   funciones  u32 n  { u32 nombre | u8 retorno | u16 nparams | u8 tipos[nparams] |
//                       u32 nslots | u32 pc_entrada | u32 pc_fin | u32 nombres_de_slot[nslots] }
//   codigo     u32 n  { u8 opcode | u8 sub | u16 argc | u32 linea | i32 dst | i32 a | i32 b | i32 args[argc] }
//
// Operando: >= 0 slot local de la funcion; < 0 constante (-(indice+1)); INT32_MIN = ninguno.
// Las etiquetas desaparecen: los saltos llevan el indice absoluto de instruccion.
// Este modulo lo ejecuta la maquina virtual (VirtualMachine.h); no es un objeto ELF/COFF.
#include "IntermediateCode.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <iomanip>
#include <stdexcept>

enum class Opc : uint8_t { Declare = 1, Move, Binary, Unary, IncDec, Cast, Call, Ret, Print, PrintLn, Jmp, JmpF, JmpT, FnEnd };
static const int32_t NO_OPERAND = std::numeric_limits<int32_t>::min();

static const char* opcName(Opc o) {
    switch (o) {
        case Opc::Declare: return "DECLARE";
        case Opc::Move: return "MOVE";
        case Opc::Binary: return "BINARY";
        case Opc::Unary: return "UNARY";
        case Opc::IncDec: return "INCDEC";
        case Opc::Cast: return "CAST";
        case Opc::Call: return "CALL";
        case Opc::Ret: return "RET";
        case Opc::Print: return "PRINT";
        case Opc::PrintLn: return "PRINTLN";
        case Opc::Jmp: return "JMP";
        case Opc::JmpF: return "JMPF";
        case Opc::JmpT: return "JMPT";
        case Opc::FnEnd: return "FNEND";
    }
    return "?";
}

struct BCInstr {
    uint8_t op = 0, sub = 0;
    int line = 0;
    int32_t dst = NO_OPERAND, a = NO_OPERAND, b = NO_OPERAND;
    vector<int32_t> args;
};
struct BCFunction {
    string name;
    uint8_t ret = T_VOID;
    vector<uint8_t> params;
    uint32_t nslots = 0, entry = 0, end = 0;
    vector<string> slotNames;
};
struct BCModule {
    vector<Value> consts;
    vector<BCFunction> funcs;
    vector<BCInstr> code;
};

class ObjectCodeGenerator {
private:
    static void putU16(vector<uint8_t>& o, uint16_t v) { o.push_back(v & 0xff); o.push_back((v >> 8) & 0xff); }
    static void putU32(vector<uint8_t>& o, uint32_t v) { for (unsigned i = 0; i < 4; ++i) o.push_back((v >> (8U * i)) & 0xffU); }
    static void putI32(vector<uint8_t>& o, int32_t v) { putU32(o, static_cast<uint32_t>(v)); }
    static void putBytes(vector<uint8_t>& o, const string& s) {
        putU32(o, static_cast<uint32_t>(s.size()));
        o.insert(o.end(), s.begin(), s.end());
    }

    struct Reader {
        const vector<uint8_t>& in; size_t pos = 0;
        explicit Reader(const vector<uint8_t>& b) : in(b) {}
        void need(size_t n) const {
            if (pos > in.size() || in.size() - pos < n) throw std::runtime_error("Objeto CVM17 truncado.");
        }
        uint8_t u8() { need(1); return in[pos++]; }
        uint16_t u16() { need(2); uint16_t v = in[pos] | (in[pos + 1] << 8); pos += 2; return v; }
        uint32_t u32() { need(4); uint32_t v = 0; for (unsigned i = 0; i < 4; ++i) v |= static_cast<uint32_t>(in[pos + i]) << (8U * i); pos += 4; return v; }
        int32_t i32() { return static_cast<int32_t>(u32()); }
        string str() {
            const uint32_t n = u32(); need(n);
            string s(reinterpret_cast<const char*>(in.data() + pos), n); pos += n; return s;
        }
    };

    // ---- pool de constantes con deduplicacion ----
    struct Pool {
        vector<Value> values; std::map<string, int32_t> index;
        int32_t add(const Value& v) {
            string key;
            switch (v.t) {
                case Value::INT: key = "i" + std::to_string(v.i); break;
                case Value::DOUBLE: { char b[40]; snprintf(b, sizeof b, "d%a", v.d); key = b; break; }
                case Value::BOOL: key = v.b ? "b1" : "b0"; break;
                case Value::STR: key = "s" + v.s; break;
                default: key = "n"; break;
            }
            auto f = index.find(key);
            if (f != index.end()) return f->second;
            values.push_back(v);
            return index[key] = static_cast<int32_t>(values.size() - 1);
        }
    };
    static int32_t constRef(int32_t poolIndex) { return -(poolIndex + 1); }

    static void validate(const BCModule& m) {
        int mains = 0;
        for (const auto& f : m.funcs) {
            if (f.name == "main") ++mains;
            if (f.entry >= m.code.size() || f.end >= m.code.size() || f.entry > f.end)
                throw std::runtime_error("Objeto CVM17: funcion '" + f.name + "' fuera del codigo.");
            if (static_cast<Opc>(m.code[f.end].op) != Opc::FnEnd)
                throw std::runtime_error("Objeto CVM17: la funcion '" + f.name + "' no termina en FNEND.");
            if (f.slotNames.size() != f.nslots || f.params.size() > f.nslots)
                throw std::runtime_error("Objeto CVM17: tabla de slots inconsistente en '" + f.name + "'.");
            auto operandOk = [&](int32_t o) {
                if (o == NO_OPERAND) return true;
                if (o >= 0) return static_cast<uint32_t>(o) < f.nslots;
                return static_cast<int64_t>(-(static_cast<int64_t>(o)) - 1) < static_cast<int64_t>(m.consts.size());
            };
            for (uint32_t pc = f.entry; pc <= f.end; ++pc) {
                const BCInstr& i = m.code[pc];
                const uint8_t raw = i.op;
                if (raw < static_cast<uint8_t>(Opc::Declare) || raw > static_cast<uint8_t>(Opc::FnEnd))
                    throw std::runtime_error("Objeto CVM17: opcode desconocido " + std::to_string(raw) + ".");
                const Opc op = static_cast<Opc>(raw);
                if (op == Opc::Jmp || op == Opc::JmpF || op == Opc::JmpT) {
                    if (i.dst < 0 || static_cast<uint32_t>(i.dst) < f.entry || static_cast<uint32_t>(i.dst) > f.end)
                        throw std::runtime_error("Objeto CVM17: salto fuera de la funcion '" + f.name + "'.");
                } else if (op == Opc::Declare || op == Opc::Move || op == Opc::Binary || op == Opc::Unary ||
                           op == Opc::IncDec || op == Opc::Cast || op == Opc::Call) {
                    if (!operandOk(i.dst)) throw std::runtime_error("Objeto CVM17: operando destino invalido.");
                }
                if (op == Opc::Call) {
                    if (i.a >= 0 || !operandOk(i.a) || m.consts[static_cast<size_t>(-(static_cast<int64_t>(i.a)) - 1)].t != Value::STR)
                        throw std::runtime_error("Objeto CVM17: CALL sin nombre de funcion valido.");
                } else if (op != Opc::Jmp && !operandOk(i.a)) {
                    throw std::runtime_error("Objeto CVM17: operando invalido.");
                }
                if (!operandOk(i.b)) throw std::runtime_error("Objeto CVM17: operando invalido.");
                for (int32_t a : i.args) if (!operandOk(a) || a == NO_OPERAND) throw std::runtime_error("Objeto CVM17: argumento invalido.");
            }
        }
        if (mains != 1) throw std::runtime_error("Objeto CVM17: se requiere exactamente una funcion main.");
    }

public:
    vector<uint8_t> generate(const IRProgram& program) const {
        Pool pool;
        BCModule mod;                       // se construye primero en memoria y luego se serializa
        struct Pending { uint32_t pc; string label; };
        for (const auto& f : program.functions) {
            BCFunction bf;
            bf.name = f.name;
            bf.ret = typeFromName(f.ret);
            if (bf.ret == T_NONE) throw std::runtime_error("Objeto CVM17: tipo de retorno no soportado '" + f.ret + "'.");
            std::map<string, int32_t> slots;
            auto slotOf = [&](const Operand& o) -> int32_t {
                auto it = slots.find(o.name);
                if (it != slots.end()) return it->second;
                int32_t id = static_cast<int32_t>(bf.slotNames.size());
                bf.slotNames.push_back(o.name);
                slots[o.name] = id;
                return id;
            };
            auto ref = [&](const Operand& o) -> int32_t {
                if (o.isNone()) return NO_OPERAND;
                if (o.isConst()) return constRef(pool.add(o.val));
                return slotOf(o);
            };
            for (const auto& p : f.params) {
                bf.params.push_back(typeFromName(p.first));
                slotOf(Operand::var(p.second));
            }
            bf.entry = static_cast<uint32_t>(mod.code.size());

            // pasada 1: pc de cada etiqueta (las etiquetas no generan instruccion)
            std::map<string, uint32_t> labelPc;
            uint32_t pc = bf.entry;
            for (const auto& i : f.code) {
                if (i.op == IROp::Label) {
                    if (!labelPc.insert({i.label, pc}).second)
                        throw std::runtime_error("Objeto CVM17: etiqueta duplicada '" + i.label + "'.");
                } else if (i.op != IROp::Nop) ++pc;
            }
            const uint32_t fnEndPc = pc;

            // pasada 2: codificacion
            for (const auto& i : f.code) {
                BCInstr b; b.line = i.line;
                switch (i.op) {
                    case IROp::Label: case IROp::Nop: continue;
                    case IROp::Declare:
                        b.op = static_cast<uint8_t>(Opc::Declare); b.dst = ref(i.dst); b.sub = typeFromName(i.text); break;
                    case IROp::Move:
                        b.op = static_cast<uint8_t>(Opc::Move); b.dst = ref(i.dst); b.a = ref(i.a); break;
                    case IROp::Binary: {
                        BinOp bop; if (!binOpFromText(i.text, bop)) throw std::runtime_error("Objeto CVM17: operador binario '" + i.text + "'.");
                        b.op = static_cast<uint8_t>(Opc::Binary); b.dst = ref(i.dst); b.a = ref(i.a); b.b = ref(i.b);
                        b.sub = static_cast<uint8_t>(bop); break;
                    }
                    case IROp::Unary: {
                        UnOp uop; if (!unOpFromText(i.text, uop)) throw std::runtime_error("Objeto CVM17: operador unario '" + i.text + "'.");
                        b.op = static_cast<uint8_t>(Opc::Unary); b.dst = ref(i.dst); b.a = ref(i.a);
                        b.sub = static_cast<uint8_t>(uop); break;
                    }
                    case IROp::IncDec:
                        b.op = static_cast<uint8_t>(Opc::IncDec); b.dst = ref(i.dst); b.a = ref(i.a);
                        b.sub = static_cast<uint8_t>((i.text == "--" ? 1 : 0) | (i.flag ? 2 : 0)); break;
                    case IROp::Cast:
                        b.op = static_cast<uint8_t>(Opc::Cast); b.dst = ref(i.dst); b.a = ref(i.a); b.sub = typeFromName(i.text); break;
                    case IROp::Call:
                        b.op = static_cast<uint8_t>(Opc::Call); b.dst = ref(i.dst);
                        b.a = constRef(pool.add(Value::Str(i.text)));
                        for (const auto& a : i.args) b.args.push_back(ref(a));
                        break;
                    case IROp::Return: b.op = static_cast<uint8_t>(Opc::Ret); b.a = ref(i.a); break;
                    case IROp::Print: b.op = static_cast<uint8_t>(Opc::Print); b.a = ref(i.a); break;
                    case IROp::PrintLn: b.op = static_cast<uint8_t>(Opc::PrintLn); break;
                    case IROp::Jump: case IROp::JumpFalse: case IROp::JumpTrue: {
                        auto t = labelPc.find(i.label);
                        if (t == labelPc.end()) throw std::runtime_error("Objeto CVM17: destino de salto inexistente '" + i.label + "'.");
                        b.op = static_cast<uint8_t>(i.op == IROp::Jump ? Opc::Jmp : i.op == IROp::JumpFalse ? Opc::JmpF : Opc::JmpT);
                        b.dst = static_cast<int32_t>(t->second);
                        if (i.op != IROp::Jump) b.a = ref(i.a);
                        break;
                    }
                }
                mod.code.push_back(std::move(b));
            }
            BCInstr end; end.op = static_cast<uint8_t>(Opc::FnEnd); end.line = f.code.empty() ? f.line : f.code.back().line;
            mod.code.push_back(end);
            bf.end = fnEndPc;
            bf.nslots = static_cast<uint32_t>(bf.slotNames.size());
            mod.funcs.push_back(std::move(bf));
        }
        mod.consts = pool.values;
        validate(mod);

        // ---- serializacion ----
        vector<uint8_t> out;
        const char magic[8] = {'C','V','M','1','7','O','B','J'};
        out.insert(out.end(), magic, magic + 8);
        putU16(out, 2); putU16(out, 0);
        putU32(out, static_cast<uint32_t>(mod.consts.size()));
        for (const Value& v : mod.consts) {
            switch (v.t) {
                case Value::INT: out.push_back(1); putI32(out, v.i); break;
                case Value::DOUBLE: {
                    out.push_back(2); uint64_t bits; static_assert(sizeof bits == sizeof v.d, "double de 64 bits");
                    std::memcpy(&bits, &v.d, sizeof bits);
                    putU32(out, static_cast<uint32_t>(bits & 0xffffffffULL)); putU32(out, static_cast<uint32_t>(bits >> 32));
                    break;
                }
                case Value::BOOL: out.push_back(3); out.push_back(v.b ? 1 : 0); break;
                case Value::STR: out.push_back(4); putBytes(out, v.s); break;
                default: out.push_back(5); break;
            }
        }
        // nombres de slot y de funcion viajan como cadenas dentro de cada entrada
        putU32(out, static_cast<uint32_t>(mod.funcs.size()));
        for (const auto& f : mod.funcs) {
            putBytes(out, f.name);
            out.push_back(f.ret);
            putU16(out, static_cast<uint16_t>(f.params.size()));
            for (uint8_t p : f.params) out.push_back(p);
            putU32(out, f.nslots); putU32(out, f.entry); putU32(out, f.end);
            for (const auto& s : f.slotNames) putBytes(out, s);
        }
        putU32(out, static_cast<uint32_t>(mod.code.size()));
        for (const auto& i : mod.code) {
            out.push_back(i.op); out.push_back(i.sub);
            putU16(out, static_cast<uint16_t>(i.args.size()));
            putU32(out, static_cast<uint32_t>(i.line < 0 ? 0 : i.line));
            putI32(out, i.dst); putI32(out, i.a); putI32(out, i.b);
            for (int32_t a : i.args) putI32(out, a);
        }
        decode(out);   // comprobacion de ida y vuelta antes de entregar el binario
        return out;
    }

    // Lee y verifica un modulo binario (firma, version, longitudes, indices, saltos y bytes sobrantes).
    static BCModule decode(const vector<uint8_t>& bytes) {
        static const char magic[8] = {'C','V','M','1','7','O','B','J'};
        if (bytes.size() < 12 || !std::equal(magic, magic + 8, bytes.begin()))
            throw std::runtime_error("Objeto CVM17: firma invalida o cabecera incompleta.");
        Reader r(bytes); r.pos = 8;
        const uint16_t version = r.u16(), flags = r.u16();
        if (version != 2 || flags != 0) throw std::runtime_error("Objeto CVM17: version o flags no soportados.");
        BCModule m;
        const uint32_t nconst = r.u32();
        if (nconst > bytes.size()) throw std::runtime_error("Objeto CVM17: pool de constantes inconsistente.");
        for (uint32_t k = 0; k < nconst; ++k) {
            const uint8_t tag = r.u8();
            switch (tag) {
                case 1: m.consts.push_back(Value::Int(r.i32())); break;
                case 2: {
                    const uint64_t lo = r.u32(), hi = r.u32(); const uint64_t bits = lo | (hi << 32);
                    double d; std::memcpy(&d, &bits, sizeof d); m.consts.push_back(Value::Dbl(d)); break;
                }
                case 3: m.consts.push_back(Value::Bool(r.u8() != 0)); break;
                case 4: m.consts.push_back(Value::Str(r.str())); break;
                case 5: m.consts.push_back(Value::Null()); break;
                default: throw std::runtime_error("Objeto CVM17: tag de constante desconocido " + std::to_string(tag) + ".");
            }
        }
        const uint32_t nfunc = r.u32();
        if (nfunc > bytes.size()) throw std::runtime_error("Objeto CVM17: tabla de funciones inconsistente.");
        for (uint32_t k = 0; k < nfunc; ++k) {
            BCFunction f;
            f.name = r.str(); f.ret = r.u8();
            const uint16_t np = r.u16();
            for (uint16_t p = 0; p < np; ++p) f.params.push_back(r.u8());
            f.nslots = r.u32(); f.entry = r.u32(); f.end = r.u32();
            if (f.nslots > bytes.size()) throw std::runtime_error("Objeto CVM17: cantidad de slots inconsistente.");
            for (uint32_t s = 0; s < f.nslots; ++s) f.slotNames.push_back(r.str());
            m.funcs.push_back(std::move(f));
        }
        const uint32_t ncode = r.u32();
        if (ncode > bytes.size() / 20U) throw std::runtime_error("Objeto CVM17 truncado: cantidad de instrucciones inconsistente.");
        for (uint32_t k = 0; k < ncode; ++k) {
            BCInstr i;
            i.op = r.u8(); i.sub = r.u8();
            const uint16_t argc = r.u16();
            const uint32_t line = r.u32();
            i.line = line > static_cast<uint32_t>(INT_MAX) ? INT_MAX : static_cast<int>(line);
            i.dst = r.i32(); i.a = r.i32(); i.b = r.i32();
            for (uint16_t a = 0; a < argc; ++a) i.args.push_back(r.i32());
            m.code.push_back(std::move(i));
        }
        if (r.pos != bytes.size()) throw std::runtime_error("Objeto CVM17: hay bytes sobrantes al final del modulo.");
        validate(m);
        return m;
    }

    static void writeBinary(const string& path, const vector<uint8_t>& bytes) {
        std::ofstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("No se pudo crear el archivo de objeto: " + path);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) throw std::runtime_error("Fallo al escribir el archivo de objeto: " + path);
    }

    // ---- listados legibles ----
    static string operandText(const BCModule& m, const BCFunction& f, int32_t o) {
        if (o == NO_OPERAND) return "_";
        if (o >= 0) {
            const string n = static_cast<size_t>(o) < f.slotNames.size() ? f.slotNames[o] : "?";
            return "r" + std::to_string(o) + "(" + n + ")";
        }
        const Value& v = m.consts[static_cast<size_t>(-(static_cast<int64_t>(o)) - 1)];
        Operand c = Operand::constant(v);
        return "k" + std::to_string(-(static_cast<int64_t>(o)) - 1) + "(" + ::operandText(c) + ")";
    }

    static string disassemble(const BCModule& m) {
        std::ostringstream out;
        out << "; CVM17 modulo objeto v2 (bytecode, no es ELF/COFF)\n";
        out << "; constantes: " << m.consts.size() << " | funciones: " << m.funcs.size() << " | instrucciones: " << m.code.size() << "\n\n";
        out << "[constantes]\n";
        for (size_t k = 0; k < m.consts.size(); ++k)
            out << "  k" << k << " = " << ::operandText(Operand::constant(m.consts[k])) << "\n";
        out << "\n[funciones]\n";
        for (const auto& f : m.funcs) {
            out << "  " << typeText(f.ret) << " " << f.name << "(";
            for (size_t p = 0; p < f.params.size(); ++p) out << (p ? ", " : "") << typeText(f.params[p]);
            out << ")  entrada=" << std::setfill('0') << std::setw(4) << f.entry << " fin=" << std::setw(4) << f.end
                << std::setfill(' ') << " slots=" << f.nslots << "\n";
        }
        out << "\n[codigo]\n";
        for (const auto& f : m.funcs) {
            out << "; --- " << f.name << " ---\n";
            for (uint32_t pc = f.entry; pc <= f.end; ++pc) {
                const BCInstr& i = m.code[pc];
                const Opc op = static_cast<Opc>(i.op);
                std::ostringstream hx; hx << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << (unsigned)i.op;
                out << std::setfill('0') << std::setw(4) << pc << std::setfill(' ') << "  " << hx.str() << "  " << std::left << std::setw(8) << opcName(op) << std::right;
                switch (op) {
                    case Opc::Declare: out << typeText(i.sub) << " " << operandText(m, f, i.dst); break;
                    case Opc::Move: out << operandText(m, f, i.dst) << " <- " << operandText(m, f, i.a); break;
                    case Opc::Binary: out << operandText(m, f, i.dst) << " <- " << operandText(m, f, i.a) << " "
                                          << binOpText(static_cast<BinOp>(i.sub)) << " " << operandText(m, f, i.b); break;
                    case Opc::Unary: out << operandText(m, f, i.dst) << " <- " << unOpText(static_cast<UnOp>(i.sub)) << " " << operandText(m, f, i.a); break;
                    case Opc::IncDec:
                        if (i.dst != NO_OPERAND) out << operandText(m, f, i.dst) << " <- ";
                        out << ((i.sub & 2) ? ((i.sub & 1) ? "--" : "++") : "") << operandText(m, f, i.a)
                            << ((i.sub & 2) ? "" : ((i.sub & 1) ? "--" : "++")); break;
                    case Opc::Cast: out << operandText(m, f, i.dst) << " <- (" << typeText(i.sub) << ") " << operandText(m, f, i.a); break;
                    case Opc::Call: {
                        if (i.dst != NO_OPERAND) out << operandText(m, f, i.dst) << " <- ";
                        out << m.consts[static_cast<size_t>(-(static_cast<int64_t>(i.a)) - 1)].s << "(";
                        for (size_t a = 0; a < i.args.size(); ++a) out << (a ? ", " : "") << operandText(m, f, i.args[a]);
                        out << ")"; break;
                    }
                    case Opc::Ret: if (i.a != NO_OPERAND) out << operandText(m, f, i.a); break;
                    case Opc::Print: out << operandText(m, f, i.a); break;
                    case Opc::Jmp: out << std::setfill('0') << std::setw(4) << i.dst << std::setfill(' '); break;
                    case Opc::JmpF: case Opc::JmpT:
                        out << operandText(m, f, i.a) << " -> " << std::setfill('0') << std::setw(4) << i.dst << std::setfill(' '); break;
                    default: break;
                }
                out << "    ; linea " << i.line << "\n";
            }
        }
        return out.str();
    }

    static string hexDump(const vector<uint8_t>& bytes, size_t limit = 96) {
        std::ostringstream out;
        for (size_t k = 0; k < bytes.size() && k < limit; k += 16) {
            out << "  " << std::setfill('0') << std::setw(4) << std::hex << k << std::dec << std::setfill(' ') << "  ";
            for (size_t j = k; j < k + 16 && j < bytes.size() && j < limit; ++j)
                out << std::uppercase << std::hex << std::setfill('0') << std::setw(2) << (unsigned)bytes[j] << std::dec << std::setfill(' ') << " ";
            out << "\n";
        }
        if (bytes.size() > limit) out << "  ... (" << bytes.size() - limit << " bytes mas)\n";
        return out.str();
    }
};
