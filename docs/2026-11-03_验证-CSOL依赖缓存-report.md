# CSOL 跨图依赖缓存验证

冻结的 4 路径缓存候选通过实际 ReHLDS SDK、完整 loader、真实 collector 和批次接线测试。8 个实际 common 模型中，原 loader 选中的 5 个特殊模型在冷、热两轮都产生同样的 80 项下载及首次出现顺序，热轮 5 次命中、0 次重建。没有改动生产代码，也没有部署、启动专服或操作游戏。

## 范围与实现边界

- 授权：父代理委派独立测试；本代理只新增测试与领域文档。
- 候选：`build/csol-dependency-cache-20261103/source-01/after.json`，4 路径；`before` 保留原 loader。
- 严格策略：所有存在性探测和实际读取内容摘要通过同一 VFS 复核；批次内共享观察、批次结束清正文，跨图只存有界依赖图。不依赖物理路径/mtime，不访问文件系统私有 ABI。
- 实际 CNG `BCryptHash` 覆盖正常摘要；独立故障开关只拦截失败分支。Windows 组件已运行；Linux 不复用分支仅源码审查，没有 Linux 构建/运行证据。
- 文件系统边界是受控替身，实际 PAK provider、完整引擎构建、真实换图和游戏 FPS 仍由父代理继续验证。

## Evidence

| ID | 来源 | 结果/内容身份 |
|---|---|---|
| E-01 | `build/csol-dependency-cache-review-20261103/sdk-05/output/result.json` | SHA-256 `24581465a3e1b4b7176539860f1aec8a72a0561e031fe34e5c8671a67ab5964b`；success=true、输入保持、unknownIncludes=0 |
| E-02 | `sdk-05/output/synthetic.log` | 66,795 个断言，0 失败；其中 65,536 个是逐输入上限断言，不能称作 66,795 个独立场景 |
| E-03 | `sdk-05/output/common.log` | 428 个断言，0 失败；8 输入/5 special/80 下载；20 个读入资源记录完整 SHA，并在基线、冷/热及结束复读保持 |
| E-04 | `sdk-05/plan.json` 与 `sdk-02` 对象来源 | 830 份冻结源/头及生成片段、4,669 个系统输入；最终 622 个实际 include 均登记；重编 1 TU、复用 8 个验证一致的 collector 对象后链接 |
| E-05 | `source-01/after.json` 与最终夹具内的完整生产源码 | loader SHA `b2913d5056ec5dd8cb1cac320a3e4eea9e3e99de5355d7180996400d1193460f`；缓存头 SHA `050dde630e98fa8f2cc7ebb4c27c2acd5c34e3d565be7579cd19c5c0d292974b`；该候选未因测试改变 |
| E-06 | [文件系统语义审查](2026-11-03_审查-CSOL依赖缓存文件系统-report.md) | 当前 stdio DLL/PDB、源校验及 7 函数字节已匹配，说明为何不用 GetLocalPath/GetFileTime 快路 |

公开复现方法见 [测试说明](../tests/csol-dependency-cache/README.md)。本机原始计划还记录编译器、SDK 环境和输入身份；输出目录均为独立文件，没有将可编辑源码硬链接到冻结快照。

## Findings

### F-01：图复用不改变资源闭包或跨图登记

- evidence_ids：E-01～E-05；status：组件已验证；confidence：high；severity：n/a_re。
- 原版完整 loader 用于真实 common 基线，避免把原先不会进入 special 分支的 SKULL-5、旋风和龙炎模型当成缓存漏收。
- 青龙、K3、黑龙炮、复活枪及带 SEQ 的僵尸模型进入原特殊分支；新版冷/热列表逐项匹配原版的首次出现顺序。
- 同批次共享引用只调用一次 `PF_precache_generic_I`；下一批次再次登记。独立入口没有遗留上一调用的 advertised 集合。
- 原样提取的 `PrecacheModelSpecifiedFiles` 验证 Begin/End 包围完整遍历、跳过空项和非 studio 模型、纹理→seqgroup→特殊依赖→原声音顺序，以及原声音 cvar 关闭时不调用原声音阶段。空批次无多余汇总。

### F-02：变化与错误走真实失效路径

- evidence_ids：E-01、E-02、E-04、E-05；status：组件已验证；confidence：high；severity：n/a_re。
- 覆盖根 MDL 同长度事件路径替换、VFS 来源切换、可选 JSON 加入/移除、common JSON 修改/移除/恢复、SEQ 缺口与末尾新增、来源描述纹理前缀变化及缺失外部贴图转存在。
- 哈希失败不发布可复用图；短读、打不开、非法 JSON 和必需资源缺失不生成半成品。文件打开后所有已测错误都关闭句柄。
- 已有正确图在一次热校验短读后可以保留，但下一批次必须完整重验证；恢复相同内容后才命中。这不代表缓存错误结果。
- 64 MiB 正文留存、65,536 个批次输入、512 条 LRU、8192 探测拒绝缓存和 16 MiB 元数据记账均覆盖。超预算允许重新读取，不会删掉依赖或缩小原 128 MiB 单文件许可范围。
- LRU 淘汰不破坏调用方持有的不可变 Result。检测到未留存正文的再次读取内容变化时，仍拒绝该准备过程。

### F-03：确认省解析，不把组件计时称为 FPS

- evidence_ids：E-03；status：单次组件观察；confidence：high（仅记录范围）；severity：n/a_re。

| 单次样本 | cache hits | rebuilt | 下载 | 实际批次读取 | 组件时间 |
|---|---:|---:|---:|---:|---:|
| 冷轮 | 0 | 5 | 80 | 17 项、约 4.4 MiB | 0.056904 秒 |
| 热轮 | 5 | 0 | 80 | 17 项、约 4.4 MiB | 0.038321 秒 |

严格内容校验依旧读取同样的 17 个输入，common 材质在每批只读一次；收益来自跳过模型解码、SEQ 拼接、JSON 和纹理依赖解析。此表没有稳定性统计，也不含完整地图加载/模型解码/客户端渲染，不能推导实际 FPS。64 MiB 是缓存留存正文上限，不是整个进程峰值；元数据限制按保守记账核验，不宣称实测 RSS。

## Path、过程与后续

- P-01，path_type=callflow：真实批次函数 Begin → 原模型遍历 → 完整新 loader → 缓存 Probe → 同一 VFS exists/read＋CNG → 热图或真实 collector → 按首次出现顺序登记 → End 清理与单行汇总。每一步由 E-01～E-05 的实际执行覆盖。
- 初轮 `sdk-01` loader 编译成功，但工具链输入登记漏了无扩展名 STL 与 Clang 内建头，脚本停在审计阶段。
- `sdk-02` 9 TU 均编译成功，链接要求完整 loader 未使用 SPR 入口的 Hunk/loadname 桩；新增“调用即失败”桩后继续。
- `sdk-03` 合成套件通过，common 因未装入导出 HUD 路径停止；`sdk-04` 改用既有导出映射后揭示夹具错误地假设全部 6 武器都 special。
- `sdk-05` 增加冻结 before 完整 loader 作真实基线，并补复活枪/SEQ 样本后全过。上述失败均只修改测试，候选生产 4 路径保持。
- 剩余：完整引擎构建、匹配模块部署、真实专服切图/重载及实际耗时。共享连续性文档、源码扫描、提交与推送由父代理统一处理；本代理不写 Git index。
