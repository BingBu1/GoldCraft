# 灾变服务端资源闭包

`tools/csol_server_table_closure.py` 对选定冻结源码、已验证的本地部署和 common 资源进行只读检查，将旧基线 ZIP 与后续更新 ZIP 按安装顺序合并，输出仍缺失或内容不同的打包输入。真实 ZIP 成员必须存在；用于省略的资源还会读取 ZIP 内字节核 SHA-256。

包含地图表、升级和角色技能表、实际角色下载 manifest、购买菜单 JSON 引用的贴图、已接入武器的明确预缓存资源，以及生产资源计划中的怪物、剧情、技能模型/声音和派生贴图。BGM 必须调用生产读表工具，从回合配置和 BSP 的 `trigger_bgm` 同时收集；缺读表工具或完整资源计划会直接失败。

运行 Python 回归：

```powershell
.tools/hlam-venv/Scripts/python.exe -X utf8 -B tests/csol-server-table-closure/test_closure.py
```

检查真实资源时传入 `--source-root`（与部署 DLL 对应的冻结源目录）、`--source-overlay`（使用了 clang VFS 时提供实际编译 overlay）、`--deployed`（已验证服务端 cstrike）、`--common`、`--map`、依次重复的 `--archive`（旧基线在前）和新的 `--output` JSON 路径。必须提供 `--resource-plan`（相同 DLL/地图完整生产计划，可重复）与 `--music-reader`（`csol_music_resources`）。用 `--dependency-list` 添加匹配的生产 `csol_resources --relocatable-json` 解析结果，覆盖模型外部纹理、SEQ 和事件资源；保留解析器身份和运行回执。输出的 `files` 可合入更新包的输入表；它不复制资源、编译或部署。

`runtime_resource_paths(text, kind)` 可解析 ReHLDS 的完整 `reslist model/sound/generic/event` 输出；总数或序号不连续会失败，声音加 `sound/`，BSP 的 `*数字` 子模型不当成文件。运行清单是另一条覆盖证据，必须按 DLL 差异剔除未发布批次的资源。原版 CS/HL 文件不在更新包中时，要单列基础安装依赖并核实际来源，不能声称已验证远端存在。

2026-11-01 回归为 20 项，增加音乐与 BSP 触发器、生成资源内容漂移、实际编译 overlay、截断预缓存清单，以及“补表后仍漏音乐和升级图”的失败场景。

验收边界：文件身份及闭包检查不等同原 C++ 表解析、正式服启动或真人入服通过。生成的角色、菜单、武器配置仍须和实际部署回执核对；不把正在开发的客户端资源混入稳定补包。
