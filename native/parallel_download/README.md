# ParallelDownload

独立 MetaHookSv x86 插件，为已核验 HL10210 引擎提供 8 路 HTTP 资源下载、加载页完成/总数与剩余计数。保留引擎的资源许可、下载开关、已有文件判断、取消和重连流程；不依赖 CSOLSupport 的内部状态。

网络线程使用 WinHTTP 直连服务器，每请求 64 KiB 缓冲、512 MiB 单文件上限；8 秒操作超时、180 秒单文件总时限，最多重试一次。4xx 不重试。完成前仅写独立临时文件，主线程才原子发布。失败文件仍由引擎原有回退流程处理，不伪装下载成功。不覆盖已有文件，未启用其他模块尚在开发的 SHA 资源替换事务。

构建：`pwsh -File tools/Build-ParallelDownload.ps1`。

生成匹配目录：`python tools/Build-ParallelDownloadGameData.py --engine <工作区内hw.dll> --output build/parallel-download/gamedata/parallel-download`。

安装 `ParallelDownload.dll` 到 `cstrike/metahook/plugins`，在现有 `plugins.lst` 末尾加入同名行；VGUI2Extension 必须已经安装。同时安装生成的 `parallel-download` 目录到 `cstrike/metahook/gamedata`。不支持的引擎或缺目录时日志明确提示禁用，保留原下载器，禁止套用偏移。

测试使用实际生产网络和队列代码：

```
python tests/parallel-download/run.py build/parallel-download/parallel_download_tests.exe build/parallel-download/test
python tests/parallel-download/run.py build/parallel-download/parallel_download_bridge_tests.exe build/parallel-download/bridge --bridge
```

每次选择新的测试输出目录，防止上轮下载文件改变冷下载条件。测试只证明故障与生命周期合同；界面位置和游戏重连还需真实客户端检查。

证据与验收边界见 `docs/2026-11-08_实现-独立并行下载-contract.md` 和 `docs/2026-11-08_交付-独立并行下载-report.md`。
