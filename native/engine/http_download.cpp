#include "precompiled.h"
#include "precache.h"
#include "http_download.h"
#include "http_download_store.h"
#include <chrono>
#include <cmath>

namespace {
cvar_t mc_http_download = {"mc_http_download", "1", 0, 1.0f, nullptr};
cvar_t mc_http_bind = {"mc_http_bind", "0.0.0.0", 0, 0.0f, nullptr};
cvar_t mc_http_port = {"mc_http_port", "27080", 0, 27080.0f, nullptr};
cvar_t mc_http_public_url = {"mc_http_public_url", "", 0, 0.0f, nullptr};
goldcraft::http::Store store;
goldcraft::http::Server server;
std::shared_ptr<goldcraft::http::Load> loading;
FileHandle_t file = nullptr;
int file_time = 0;
std::string advertised, bind;
unsigned short port = 0;
double next_config_check = 0, max_read_ms = 0;
bool config_checked = false, download_allowed = false;

void CloseRead(bool success) {
    if (file) { FS_Close(file); file = nullptr; }
    if (loading) { store.finish(loading, success); loading.reset(); }
}
void ClearAdvertised() {
    if (!advertised.empty() && advertised == sv_downloadurl.string) Cvar_Set("sv_downloadurl", "");
    advertised.clear();
}
void Stats() {
    const auto s = store.stats(); const auto n = server.stats();
    Con_Printf("GoldCraft built-in HTTP: TCP %u, resources=%u, reserved=%u, hits=%llu loads=%llu failures=%llu\n",
        server.port(), static_cast<unsigned>(s.files), static_cast<unsigned>(s.reserved), s.hits, s.loads, s.failures);
    Con_Printf("HTTP requests=%llu bytes=%llu rejected=%llu max_read_frame_ms=%.3f public_url=%s\n",
        n.requests, n.bytes, n.rejected, max_read_ms, advertised.empty() ? "not configured" : advertised.c_str());
}
void Configure() {
    const auto value = mc_http_port.value;
    const auto desired = std::isfinite(value) && value >= 1 && value <= 65535 && value == std::floor(value)
        ? static_cast<unsigned short>(value) : 0;
    download_allowed = sv_allow_download.value != 0 && sv_send_resources.value != 0;
    const bool enabled = mc_http_download.value != 0 && desired != 0 && download_allowed;
    if (config_checked && bind == mc_http_bind.string && port == (enabled ? desired : 0) &&
        (!enabled || server.port())) {
        // URL can change independently of the listener, including FRP mapping.
    } else {
        CloseRead(false); store.publish({}); ClearAdvertised(); server.stop();
        bind = mc_http_bind.string; port = enabled ? desired : 0; config_checked = true;
        if (enabled && !server.start(bind, port, [](const std::string& name) { return store.request(name); })) {
            Con_Printf("GoldCraft HTTP: cannot bind TCP %s:%u; UDP downloads remain available.\n", bind.c_str(), port);
            return;
        }
        if (enabled) Con_Printf("GoldCraft built-in HTTP listening on TCP %s:%u\n", bind.c_str(), port);
        // Rebuild from the actual completed engine resource list.
        if (enabled) GC_HttpResources();
    }
    const std::string url = mc_http_public_url.string;
    if (!enabled || !server.port() || url.empty()) { ClearAdvertised(); return; }
    if (!goldcraft::http::public_url(url)) {
        ClearAdvertised();
        return;
    }
    if (advertised != url) {
        Cvar_Set("sv_downloadurl", url.c_str());
        advertised = url;
        // sv_allow_dlfile already defaults to 1. Respect an admin's explicit
        // denial rather than silently changing the native download policy.
    }
}
} // namespace

void GC_HttpInit() {
    Cvar_RegisterVariable(&mc_http_download); Cvar_RegisterVariable(&mc_http_bind);
    Cvar_RegisterVariable(&mc_http_port); Cvar_RegisterVariable(&mc_http_public_url);
    Cmd_AddCommand("mc_http_stats", Stats);
    Cmd_AddCommand("mc_http_refresh", GC_HttpResources);
    // Service package passes configuration through the child environment,
    // avoiding command-line quoting and applying it before the first map.
    const char* names[] = {"GOLDCRAFT_HTTP_ENABLED", "GOLDCRAFT_HTTP_BIND", "GOLDCRAFT_HTTP_PORT", "GOLDCRAFT_HTTP_PUBLIC_URL"};
    const char* cvars[] = {"mc_http_download", "mc_http_bind", "mc_http_port", "mc_http_public_url"};
    for (int i = 0; i < 4; ++i) if (const auto* value = getenv(names[i])) Cvar_Set(cvars[i], value);
}
void GC_HttpResources() {
    CloseRead(false);
    std::vector<goldcraft::http::Resource> resources;
    if (mc_http_download.value != 0 && sv_allow_download.value != 0 && sv_send_resources.value != 0)
      for (const auto& resource : gc_precache.resources) {
        if (resource.type == t_decal || resource.nDownloadSize <= 0 ||
            resource.szFileName[0] == '*' || resource.szFileName[0] == '!' || (resource.ucFlags & RES_CUSTOM)) continue;
        const auto name = std::string(resource.type == t_sound ? "sound/" : "") + resource.szFileName;
        resources.push_back({name, static_cast<std::size_t>(resource.nDownloadSize)});
    }
    store.publish(resources);
}
void GC_HttpFrame() {
    if (realtime >= next_config_check || download_allowed != (sv_allow_download.value != 0 && sv_send_resources.value != 0)) {
        next_config_check = realtime + 1; Configure();
    }
    if (!server.port()) return;
    const auto begin = std::chrono::steady_clock::now();
    // Bounded total main-thread IO; worker threads never touch the non-thread-
    // safe engine filesystem. Handles for PAK files share an underlying FILE,
    // so always seek explicitly before each chunk, after other engine reads.
    std::size_t budget = 256 * 1024;
    while (budget) {
        if (!loading) {
            loading = store.take();
            if (!loading) break;
            file = FS_Open(loading->name.c_str(), "rb");
            if (!file || FS_Size(file) != loading->size) { CloseRead(false); break; }
            file_time = FS_GetFileTime(loading->name.c_str());
            loading->bytes->reserve(loading->size);
        }
        const auto offset = loading->bytes->size();
        const auto remaining = loading->size - offset;
        const auto count = remaining < budget ? remaining : budget;
        loading->bytes->resize(offset + count);
        FS_Seek(file, static_cast<int>(offset), FILESYSTEM_SEEK_HEAD);
        const auto got = FS_Read(loading->bytes->data() + offset, static_cast<int>(count), 1, file);
        budget -= count;
        if (got != static_cast<int>(count) || !FS_IsOk(file)) { CloseRead(false); break; }
        if (loading->bytes->size() == loading->size) {
            CloseRead(FS_Size(file) == loading->size && FS_FileSize(loading->name.c_str()) == loading->size &&
                      FS_GetFileTime(loading->name.c_str()) == file_time);
        }
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    if (elapsed > max_read_ms) max_read_ms = elapsed;
}
void GC_HttpReset() { CloseRead(false); store.publish({}); }
void GC_HttpShutdown() {
    GC_HttpReset(); ClearAdvertised(); server.stop(); config_checked = false; next_config_check = 0;
}
