#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>

using std::string;
using std::vector;

class SymbolTable {
private:
    struct Symbol { string type; bool isConst = false; };
    vector<std::unordered_map<string, Symbol>> scopes;
public:
    void clear() { scopes.clear(); }
    void push() { scopes.emplace_back(); }
    void pop() {
        if (scopes.empty()) throw std::logic_error("SymbolTable::pop sin ambito activo");
        scopes.pop_back();
    }

    // C++ permite ocultar nombres de ambitos exteriores, pero no redeclararlos
    // dentro del mismo bloque. La constancia pertenece al simbolo, no al tipo.
    bool insert(const string& name, const string& type, bool isConst = false) {
        if (scopes.empty()) throw std::logic_error("SymbolTable::insert sin ambito activo");
        if (scopes.back().count(name)) return false;
        scopes.back()[name] = Symbol{type, isConst};
        return true;
    }

    bool lookup(const string& name, string& type) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) { type = f->second.type; return true; }
        }
        return false;
    }

    bool isConst(const string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second.isConst;
        }
        return false;
    }
};
