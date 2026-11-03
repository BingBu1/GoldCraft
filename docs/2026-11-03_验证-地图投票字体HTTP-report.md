# 地图投票字体 HTTP 下载修复

已确认并修复版本化地图投票字体的 HTTP 404：旧引擎只允许 `resource/font/` 内的 TTF，实际地图广告使用 `resource/csol/mapvote/v1/df_gb_y9.ttf`。候选只将 `resource/csol/` 纳入 TTF 目录许可；资源广告门、路径校验、其他扩展名许可和现有尺寸限制保持。

独立组件、完整引擎增量构建，以及隔离专服的实际广告→HTTP 全文验证均通过。测试后专服文件全部恢复，B 未修改。**尚未验证 B 首次下载、VGUI 字体创建或人物技能崩溃；没有部署正式服务器。**

## 证据与结论

| 证据 | 结果 | 证据目录 |
|---|---|---|
| 旧真实专服、相同字体存在且已广告 | HTTP 404，3,422,064 B 文件本身存在 | `build/csol-skill-crash-audit-20261103/server-live-02/result.json` |
| 旧对象的真实 Winsock/引擎适配器对照 | 48 行为、323 引擎边界检查通过，字体预期 404 | `build/csol-mapvote-font-http-20261103/component-03/output/before-run.log` |
| 修复对象、相同原字体负载 | 57 行为、507 边界检查通过，HTTP 200，完整长度和 SHA 一致 | 同目录 `after-run.log`、`result.json` |
| 大文件与目录限制专项 | 83 行为、2,095 边界检查通过；22,309,736 B 字体 SHA 一致 | 同目录 `large-run.log` |
| 完整引擎增量构建 | 1 TU 和 1 链接通过；6,039 输入保持、399 实际头文件、133 链接输入均核对 | `engine-full-01/result.json` |
| 新引擎实际加载到隔离专服 | 字体、HUD scheme、开局 BGM 均有真实广告，HTTP 200、Content-Length 和全文 SHA 一致 | `server-live-01/result.json` |
| 退出与恢复 | 自有 PID 43324 已结束，7 项事务全部恢复；B、配置、原引擎、保护安装内容保持 | 同文件及 `proposal.json` |

组件还验证了 HEAD、尾部 Range、大小写路径和未广告字体拒绝；路径负例包括越级、编码越级、重复分隔符、反斜杠、查询串、ADS、伪扩展名以及未许可的 `.res`。没有放开其他目录的任意字体读取。

## 完整引擎身份

使用已经验证的换图依赖缓存引擎 `3d492a06781439280cfcbb8e75fa46822e38c1b67588b891eea48c5494a0eb51` 为基线，只替换 `http_download_server.obj`。其余实际链接对象、库与路径保持，未降级换图缓存优化。

- `swds.dll`：2,358,784 B，SHA `c0d3afc1f83f5ffda630ea5ac341a33f0b24496da1793c492c2cc6e8b2b2f2b4`。
- `swds.pdb`：19,681,280 B，SHA `c551ffde46360b5352500491b0799cdb77db7f3457cf17bb60ff9bd55be61579`。
- x86；PE/PDB GUID `093fc7e6-702e-ae44-4c4c-44205044422e`，age 1，配对相同。
- 单 job；该 TU 4.43 秒，链接 32.18 秒。保持 C++20、O3、ThinLTO、AVX2、precise 与 `ffp-contract=off`。

产物在 `build/csol-mapvote-font-http-20261103/engine-full-01/output/`，构建计划、实际依赖和产品身份同目录上层留存。

## 实际专服抽查

专服使用 `zs_deadend`，配套 `mp.dll 60ad5e51...`、`goldcraft_amxx.dll 444094c5...`、`filesystem_stdio.dll 14bedc21...`、ReAPI `2387f504...`。五个模块均按实际进程路径核 SHA。读取到 1,569 项 generic 资源广告，只下载以下 3 个样本，没有重传此前通过的 994 项全集：

| 路径 | 字节 | SHA-256 |
|---|---:|---|
| `resource/csol/mapvote/v1/df_gb_y9.ttf` | 3,422,064 | `59eeaf021f611ce21ec41ce245d19efeabe90710c57d36f01b28d6069c320d55` |
| `resource/csol/hud/v1/CSOLHudScheme.res` | 792 | `355699d46428ec6ba4138e6bda94b2b0eac06404df06754b7d45c21483f9f01b` |
| `bgm/Scenario_Ready.mp3` | 303,287 | `c10543ba674906ae8423adf51d31b186d25bbca79e7c22b8f38e896db3ddbe1d` |

6 个 DLL/PDB 与临时 qconsole 共 7 项文件事务使用独立临时文件校验后原子替换。测试结束恢复原内容，停止自身进程，释放 27045 和 28080。实际运行脚本原件及 SHA 保存在 `server-live-01/source/run_live_font.py`；只读取保护安装的基线，不在其内写入或启动进程。

## 单 B 后续清单

`build/csol-skill-crash-audit-20261103/retest-plan-03/deployment-plan.json` 取代 `retest-plan-02` 的字体预填与旧引擎方案。现在是 **10 个 B 文件、6 个专服文件**，每项记录源/目标、完整 SHA、旧内容、文件 ID 和链接数。

B 仍需 registry 客户端 DLL/PDB、两份原函数符号目录、已冻结 v4/v6 的五个呈现文件，以及原位置的角色目录修正。专服使用 mp/AMXX 各 DLL/PDB，加上这次验证的 swds DLL/PDB；filesystem 保持。

删除了旧方案中预先复制字体到 B 的动作。B 的潜在内容根当前仍无这份字体，入服应通过 HTTP 获取它及此前验证的 175 项缺件，共 176 项。实际下载后必须再核新字体长度和 SHA，并观察字体创建；服务端 HTTP 成功不能代替这些客户端证据。

人物技能复测仍从角色 165／scenarioClass 9889 持 K3 开始，再覆盖 132／8659 与 133／8660、技能结束、切换、死亡重生和重连。使用既有完整 dump 采集方案，继续区分技能 `hw+1BB099` 与退出 `hw+1BB006`。本阶段没有定位或宣称修复这两个异常。

## 公开源码和测试入口

生产改动：`native/engine/http_download_server.cpp`。公开专项：`tests/csol-http-large-resource/large_http.cpp`，沿用同目录 `prepare.py`、`run.py` 和 `README.md` 的单 job 入口。测试使用调用者提供的字体和 scheme，不随源码发布原游戏资源。

这次精确冻结为 `build/csol-mapvote-font-http-20261103/source-01/source.json`：生产 TU SHA `44344e612838803bcbbe543159cf5154cb45478fe27469a99e6536f9858f595a`，公开测试 SHA `0cec83efc086a5978b72e3232ccff6820efd8217bf5822a371848e141f46bdbb`。独立原字体对照夹具另存 `mapfont_probe.cpp`，基于项目现有 `native/engine/tests/http_download_tests.cpp` 的真实 HTTP 适配器，不引入模拟 HTTP 服务器。

准备阶段保留两项工具失败：第一次使用了错误的字体 SHA 常量，在编译前被断言停止；之后旧版链接成功，但输入审计发现 CRT 的 `libcmt.i386.pdb` 未预登记。修正前者为读取真实专服证据，补齐后者的库 PDB 预登记，复用已编译的 probe 对象后完成组件验证。首败分别保存在 `component-01`、`component-02`，没有通过删除记录或改生产行为绕过失败。

未写 Git index 或发布。源码提交与正式部署由父任务按已核冻结版本继续处理；无需重跑本阶段通过的组件和三个专服 HTTP 样本。
