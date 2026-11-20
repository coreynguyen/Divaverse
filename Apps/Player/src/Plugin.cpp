#include "Plugin.h"
#if DV_PLUGINS
#include <algorithm>
namespace {
struct Loaded { HMODULE mod; const DvpPlugin* p; std::wstring file; };
std::vector<Loaded> g_plugins; std::wstring g_dataDir;
void HostLog(const char* s) { Log("plugin: %s", s ? s : ""); }
const wchar_t kScheme[] = L"plugin://";
}
void PluginsLoad() {
    wchar_t exe[MAX_PATH * 2]; GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    std::wstring dir = JoinPath(DirOf(exe), L"Plugins");
    if (!DirExists(dir)) return;
    g_dataDir = AppDataDir() + L"\\PluginData"; CreateDirectoryW(g_dataDir.c_str(), nullptr);
    static DvpHost host; host = { DVP_ABI, HostLog, g_dataDir.c_str() };
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*.dll").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring f = JoinPath(dir, fd.cFileName);
        HMODULE m = LoadLibraryExW(f.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);     // its own folder first, for DLLs it brings along
        if (!m) { Log("plugin %s: cannot load (error %lu)", WToUtf8(fd.cFileName).c_str(), GetLastError()); continue; }
        auto init = (const DvpPlugin*(*)(const DvpHost*))(void*)GetProcAddress(m, "DvPluginInit");
        const DvpPlugin* p = init ? init(&host) : nullptr;
        bool ok = p && p->abi == DVP_ABI && p->id && *p->id && p->scan && p->open && p->read && p->close;
        if (ok) for (const wchar_t* c = p->id; *c; ++c) ok = ok && (iswalnum(*c) || *c == L'_' || *c == L'-');
        if (ok) for (auto& l : g_plugins) ok = ok && _wcsicmp(l.p->id, p->id) != 0;
        if (!ok) { Log("plugin %s: %s", WToUtf8(fd.cFileName).c_str(), !init ? "no DvPluginInit export" : !p ? "DvPluginInit returned nothing" : "wrong interface version, bad id or missing functions"); FreeLibrary(m); continue; }
        g_plugins.push_back({ m, p, fd.cFileName });
        Log("plugin %s: '%s' %s (id %s)", WToUtf8(fd.cFileName).c_str(), WToUtf8(p->name ? p->name : L"").c_str(), WToUtf8(p->version ? p->version : L"").c_str(), WToUtf8(p->id).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
void PluginsUnload() { for (auto& l : g_plugins) { if (l.p->shutdown) l.p->shutdown(); FreeLibrary(l.mod); } g_plugins.clear(); }
size_t PluginCount() { return g_plugins.size(); }
const DvpPlugin* PluginAt(size_t i) { return i < g_plugins.size() ? g_plugins[i].p : nullptr; }
bool PluginIsPath(const std::wstring& p) { return p.size() > 9 && !_wcsnicmp(p.c_str(), kScheme, 9); }
std::wstring PluginPath(const wchar_t* pid, const wchar_t* item) { return std::wstring(kScheme) + pid + L"/" + item; }
const DvpPlugin* PluginFor(const std::wstring& path, std::wstring& item) {
    if (!PluginIsPath(path)) return nullptr;
    size_t s = path.find(L'/', 9); if (s == std::wstring::npos) return nullptr;
    std::wstring id = path.substr(9, s - 9); item = path.substr(s + 1);
    for (auto& l : g_plugins) if (!_wcsicmp(l.p->id, id.c_str())) return l.p;
    return nullptr;
}
std::wstring PluginSummary() {
    std::wstring s; for (auto& l : g_plugins) s += (s.empty() ? L"" : L", ") + std::wstring(l.p->name ? l.p->name : l.p->id) + L" " + (l.p->version ? l.p->version : L"");
    return s;
}
#else
void PluginsLoad() {} void PluginsUnload() {} size_t PluginCount() { return 0; } const DvpPlugin* PluginAt(size_t) { return nullptr; }
bool PluginIsPath(const std::wstring&) { return false; } std::wstring PluginPath(const wchar_t*, const wchar_t*) { return L""; }
const DvpPlugin* PluginFor(const std::wstring&, std::wstring&) { return nullptr; } std::wstring PluginSummary() { return L""; }
#endif
