# 大资源 HTTP 专项

两个套件都编译实际 `http_download.cpp`、Store、HTTP Server 和 LauncherFiles，使用窄引擎 API 夹具及本机随机端口的真实 Winsock。文件系统夹具断言只在主线程读取，不启动 CS 专服或图形客户端。

- 默认套件传输原 `sarasa_ui_sc.ttf` 全部 22,309,736 字节，并逐字及 SHA 核对正文；覆盖 Content-Length、跨 16 MiB Range、HEAD、416、64 MiB Store 边界及固定预算、地图取消、截短/变更读取、HUD scheme 精确路径和启动器原尺寸限制。
- `--full-suite` 使用现有 `native/engine/tests/http_download_tests.cpp`，覆盖真实 Winsock 背压、整文件与 Range、喷漆上传/下载转发，以及原 Store、路由和逐帧适配器回归。

先准备，再运行。所有 C++ 文件从当前项目 `native/` 和 `tests/` 冻结，不依赖历史 build 目录。`--environment` 是调用者准备的 JSON 字典，包含匹配 x86 MSVC 和 Windows SDK 的 `PATH`、`INCLUDE`、`LIB` 等环境变量；编译器/链接器也由参数指定。该文件可能含本机路径，勿发布。编译固定单 job、C++20、O3、ThinLTO、AVX2、precise 和禁用浮点收缩。

```text
python tests/csol-http-large-resource/prepare.py --output build/http-large-test --compiler <clang-cl.exe> --linker <lld-link.exe> --environment <environment.json> --font <common/resource/Font/sarasa_ui_sc.ttf> --scheme <prepared/resource/csol/hud/v1/CSOLHudScheme.res>
python tests/csol-http-large-resource/run.py build/http-large-test
```

完整套件另用新的 `--output` 并加 `--full-suite`。可选 `--source-manifest` 接受带 `path`（项目相对路径）和 `sha256` 的数组，用于要求特定评审版本。准备过程不编译；运行遇首败保留日志并停止，产物仅在指定 build 目录。不能把这两个套件当作真人客户端的首次下载或地图玩法验收。
