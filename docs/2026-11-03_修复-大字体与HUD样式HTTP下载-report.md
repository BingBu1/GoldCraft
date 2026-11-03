# 大字体与 HUD 样式 HTTP 下载

失落之城完整资源实服检查在已成功下载 55 项后，被 `sarasa_ui_sc.ttf` 的 HTTP 404 阻断。文件实际存在且有 22,309,736 字节；内置 HTTP 错把资源列表的 24 位大小上限当成正文上限，没有发布这个文件。随后必需的 `CSOLHudScheme.res` 也会被路径规则拒绝。

## 原因与修正

| Evidence | Finding | Path |
|---|---|---|
| `live-02/result.json` 的资源广告与第一次 HTTP 404，原字体 SHA 一致 | 不是解包缺失；`Store::publish` 过滤超过 16,777,215 字节的资源 | 普通资源正文限额改为 64 MiB，与现有缓存总预算一致 |
| 匹配 B 引擎的 HTTP 完成回调与 `FS_Write`，9 组受控原指令回放 | HTTP 正文长度来自 SteamHTTP 的完整 uint32；24 位资源大小仅作预计下载量，ReHLDS 饱和到最大值 | 保留原资源消息；使用原 HTTP 全长接收路径 |
| `resource_name` 与客户端 HUD 的精确 `SchemePath` | 必需 `.res` 未被允许 | 仅增加 `resource/csol/hud/v1/CSOLHudScheme.res` |
| 启动器与资源服务器原先共用 `max_file_size` | 直接提高共享常量会连带扩大启动器负载限额 | 启动器仍保留原 16 MiB−1，manifest 仍为 1 MiB |

保持资源预缓存清单、64 MiB 缓存总预算、每帧最多读取 256 KiB、文件大小和修改时间复核、换图取消及喷漆独立限额。没有开放任意 `resource/*.res`，也没有改写字体或跳过缺失资源检查。相关源码在 `native/engine/http_download_server.h`、`http_download_server.cpp` 和 `launcher_http.cpp`。

原协议证据详见 `2026-11-03_分析-大字体FastDL长度兼容-report.md`。本次仅确认所核普通 HTTP 路径，不把它推广为自定义 HPAK 上传或无限大小文件支持。

## 验证与交付边界

父代理已复核协议交接 17 个引用和 live-02 的 81 个证据/恢复文件；原回调回放 9 组通过，完整字体落盘 SHA 与来源一致。后台实际专服此前已确认四个目标模块加载、2573 项资源广告和 55 项 HTTP 全长 SHA，尚未执行五轮玩法。

修复后的独立专项通过 72 项行为和 1614 项引擎调用边界检查，真实原字体正文、Content-Length、跨 16 MiB Range、HEAD、416、缓存边界、换图取消、截短/变更读取、精确 scheme 路由和启动器原限额均通过。完整 HTTP 套件另外通过 1706 项检查；四个真实 Winsock 背压组覆盖整文件、Range、喷漆下载与上传转发，正文均逐字相同，没有重复 64 KiB 数据。

完整 ReHLDS 四个受头文件影响的 TU 和链接全部成功。初次后验将大写 `HTTP_DOWNLOAD_STORE.OBJ` 误判为未知输入；保留原失败回执，按 Windows 完整路径、文件身份及 SHA 重新核验，未重编或跳过审计。最终 5890 个登记输入、610 个实际 include 和 132 个链接输入无未知项，父代理独立复核 5914 个唯一证据/输入文件。新 x86 DLL SHA 为 `848330b7afd28989bca6a672d311ebcdc48dd4fc24ff5fc83d363196fb622808`，PDB 匹配；保留 C++20、O3、ThinLTO、AVX2、precise。

这批源码还包含同一已测 HTTP 文件中此前未发布的背压发送修复及剧情资源路径，完整套件在当前组合重新验证。复现入口在 `tests/csol-http-large-resource/README.md`，所有产品源从当前项目冻结，环境与原字体由显式参数提供，不依赖历史 build 目录。

实服五轮及冷客户端下载仍待验证，不能把原回调受控回放算作真实 Steam 网络下载或图形验收。没有更新正式远端服务器，没有恢复已暂停的界面操作。

本地证据目录：`build/csol-http-large-resource-20261103`；客户端长度证据：`build/csol-large-fastdl-20261103`；专服首败与恢复：`build/csol-skill6-client-messages-20261103/live-02`。生产三文件前后快照在 `source-01`。
