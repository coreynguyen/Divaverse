#pragma once
// Minimal JSON reader (RFC 8259 subset sufficient for manifests). No deps.
#include <string>
#include <vector>
#include <utility>

struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;

    const JVal* get(const char* k) const {
        if (t != Obj) return nullptr;
        for (auto& kv : o) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    double num(const char* k, double d) const { auto* v = get(k); return v && v->t == Num ? v->n : d; }
    bool   boolean(const char* k, bool d) const { auto* v = get(k); return v && v->t == Bool ? v->b : d; }
    std::string str(const char* k, const std::string& d = {}) const { auto* v = get(k); return v && v->t == Str ? v->s : d; }
};

bool JsonParse(const std::string& text, JVal& out, std::string* err = nullptr);
