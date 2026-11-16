#include "Json.h"
#include <cstdlib>
#include <cstring>

namespace {
struct P {
    const char* p; const char* e; std::string err;
    void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    bool fail(const char* m) { if (err.empty()) err = m; return false; }
    static void utf8(std::string& o, unsigned c) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
        else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
    }
    bool hex4(unsigned& v) {
        if (e - p < 4) return fail("bad \\u");
        v = 0; for (int i = 0; i < 4; ++i) { char c = *p++; v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; else return fail("bad hex"); }
        return true;
    }
    bool str(std::string& o) {
        ++p;
        while (p < e && *p != '"') {
            if (*p == '\\') {
                if (++p >= e) return fail("eof in escape");
                switch (*p++) {
                case '"': o += '"'; break; case '\\': o += '\\'; break; case '/': o += '/'; break;
                case 'b': o += '\b'; break; case 'f': o += '\f'; break; case 'n': o += '\n'; break;
                case 'r': o += '\r'; break; case 't': o += '\t'; break;
                case 'u': { unsigned c; if (!hex4(c)) return false;
                    if (c >= 0xD800 && c < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2; unsigned lo; if (!hex4(lo)) return false; c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); }
                    utf8(o, c); break; }
                default: return fail("bad escape");
                }
            } else o += *p++;
        }
        if (p >= e) return fail("unterminated string");
        ++p; return true;
    }
    bool val(JVal& v, int depth) {
        if (depth > 64) return fail("too deep");
        ws(); if (p >= e) return fail("eof");
        char c = *p;
        if (c == '{') {
            v.t = JVal::Obj; ++p; ws();
            if (p < e && *p == '}') { ++p; return true; }
            for (;;) {
                ws(); if (p >= e || *p != '"') return fail("expected key");
                std::string k; if (!str(k)) return false;
                ws(); if (p >= e || *p != ':') return fail("expected :"); ++p;
                v.o.emplace_back(std::move(k), JVal{});
                if (!val(v.o.back().second, depth + 1)) return false;
                ws(); if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; return true; }
                return fail("expected , or }");
            }
        }
        if (c == '[') {
            v.t = JVal::Arr; ++p; ws();
            if (p < e && *p == ']') { ++p; return true; }
            for (;;) {
                v.a.emplace_back(); if (!val(v.a.back(), depth + 1)) return false;
                ws(); if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; return true; }
                return fail("expected , or ]");
            }
        }
        if (c == '"') { v.t = JVal::Str; return str(v.s); }
        if (!strncmp(p, "true", e - p < 4 ? e - p : 4) && e - p >= 4)  { v.t = JVal::Bool; v.b = true;  p += 4; return true; }
        if (!strncmp(p, "false", e - p < 5 ? e - p : 5) && e - p >= 5) { v.t = JVal::Bool; v.b = false; p += 5; return true; }
        if (!strncmp(p, "null", e - p < 4 ? e - p : 4) && e - p >= 4)  { v.t = JVal::Null; p += 4; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            std::string num; while (p < e && strchr("+-0123456789.eE", *p)) num += *p++;
            char* end = nullptr; v.n = strtod(num.c_str(), &end); v.t = JVal::Num;
            return end && *end == 0 ? true : fail("bad number");
        }
        return fail("unexpected char");
    }
};
}

bool JsonParse(const std::string& text, JVal& out, std::string* err) {
    P ps{ text.data(), text.data() + text.size(), {} };
    out = JVal{};
    bool ok = ps.val(out, 0);
    if (ok) { ps.ws(); if (ps.p != ps.e) ok = ps.fail("trailing data"); }
    if (!ok && err) *err = ps.err + " at offset " + std::to_string(ps.p - text.data());
    return ok;
}
