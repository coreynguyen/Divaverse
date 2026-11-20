#pragma once
// Host side of the content plugin interface (DvPlugin.h). Everything plugin-related in the Player goes through the
// few functions below and is compiled only when DV_PLUGINS is 1 (Common.h). With no "Plugins" folder nothing happens.
#include "Common.h"
#include "DvPlugin.h"

void PluginsLoad();                                        // Plugins\*.dll beside the exe; call once before the first library scan
void PluginsUnload();
size_t PluginCount();
const DvpPlugin* PluginAt(size_t i);
bool PluginIsPath(const std::wstring& path);               // "plugin://<plugin id>/<item id>"
std::wstring PluginPath(const wchar_t* pluginId, const wchar_t* itemId);
const DvpPlugin* PluginFor(const std::wstring& path, std::wstring& itemId);   // nullptr: that plugin is not installed
std::wstring PluginSummary();                              // for the diagnostics report
