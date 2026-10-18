#pragma once
// The offline runner owns an AMX directly, without a loaded engine plugin.
class CPluginMngr {
public:
    class CPlugin {
    public:
        bool isDebug() const { return false; }
        AMX* getAMX() { return nullptr; }
        const char* getName() const { return nullptr; }
    };
    CPlugin* findPluginFast(AMX*) { return nullptr; }
};
inline CPluginMngr g_plugins;
