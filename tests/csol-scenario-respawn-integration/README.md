# 灾变复活服务端增量检查

本组运行实际生产消息方法或 Session 资源语句，使用匹配 ReGameDLL SDK；它不启动游戏、不构建完整 DLL，也不替代真实复活交易与多人测试。准备器依赖工作区已有 `build/csol-frozen-death-20261101/fresh-combination-01` 的编译环境与冻结依赖。缺少该本地基线时不能直接复现。

- `messages.cpp`、`rules.cpp`：既有复活 writer 与失败过渡；默认准备范围。
- `retry_messages.cpp`：两个新增续关广播，`--only retry_messages`。
- `precache.cpp`：逐字提取 Session 的复活资源插入及注册段，`--only precache`。真实引擎回调类型、地图许可/身份边界和缺项短路；不运行无关地图表加载。
- `resources.py`：实际两个资源收集入口及生成器主流程，控制无关模型/地图读取器；仅读已解包 common、交付 ZIP 和旧基线 ZIP，验证路径与 SHA，不复制资源。

在项目根目录运行，输出目录必须尚不存在：

```powershell
& .tools/hlam-venv/Scripts/python.exe -B tests/csol-scenario-respawn-integration/prepare.py --only precache --output build/csol-respawn-precache-check
& .tools/hlam-venv/Scripts/python.exe -B build/csol-respawn-precache-check/run.py
```

第一条只准备，不编译。第二条需使用已协调的单 job 编译窗口，顺序编译、链接、运行，首败停止并保留证据。

资源检查使用当前两个工具和历史 Session 作对照：

```powershell
& .tools/hlam-venv/Scripts/python.exe -B tests/csol-scenario-respawn-integration/resources.py --output build/csol-respawn-resource-check --old-source build/csol-frozen-death-20261101/fresh-combination-01/source --common 'G:/New NARTOOL/CsolTools/pak解压/UnCsoPak/lstrike/common' --delivered-receipt build/remote-bgm-20261101/delivery-final.json --baseline-audit build/csol-server-table-closure-20261101/bgm-three-packages-audit-final.json
```

2026-11-01 的 `precache-validation-01` 为 381 项、三步骤全 0、551 个实际 include 且未知 0；`resources-03` 为 80 项全过、27 项共享资源。旧 UI 和 writer 的已过套件没有重复执行。完整 DLL、资源更新、HTTP 下载和游戏验收仍由合批任务完成。
