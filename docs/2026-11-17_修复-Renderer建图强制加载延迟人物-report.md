# Renderer 建图强制加载延迟人物

已修改建图模型枚举：光照、世界资源和资源重载扫描共同使用独立服务器媒体接口，保留人物按需加载。当前为源码／组件验证阶段，完整 DLL、部署和新的跨图性能验收分别记录；不能据此宣布换图超时、空进度条或未响应已解决。

## 证据与结论

| ID | 证据 | 已证事实和边界 |
| --- | --- | --- |
| E-001 | `build/map-change-live-20261117-02/cross-lostcity.json`，SHA `5b3667cc659f87724af14f4ab5e79d8290cf8635e1681935d37c13d8f5deee04` | 18,902 个实际主线程上下文，采集失败 0、未附加调试器；覆盖约 55 秒、进程累计 CPU 增加 31.8125 秒、读取字节增加 1,564,604,430。包含等待，不是独占 CPU 或同条件 A/B 提速 |
| E-002 | 同轮完整符号解析与 `resolved-summary.json` | 1,450 个栈包含 LoadStudio，其中 1,306 个同时经过 GL_BuildLightmaps。完整数据修正早期只看到局部解析的范围；其余加载仍在原生预缓存链，不声称所有模型成本都来自 Renderer |
| E-003 | 宿主 hw.dll，x86，SHA `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`，基址 `0x10000000` | CL_GetModelByIndex VA `0x101A2FA0`／RVA `0x1A2FA0` 的 184 字节、62 条指令与 IDA／PE／Capstone 逐项核同。读取服务器模型表 `0x113F6410`；needload 位于模型 `+0x40`，值 1 或 2 时调用 Mod_LoadModel `0x10240290`，其他状态直接返回 |
| E-004 | `analysis/renderer-lazy-models-20261117/PREEDIT.json`、`getter-verification.json` | 修改前记录原样本、调用链和所有修改路径输入 SHA。初次 IDA 仅定义第一条指令的输出未用于实现；补逐条定义并核同完整地址序列后才修改生产源码 |
| E-005 | 真实生产代码的 SDK 联动夹具，`tests/renderer-model-scan` | 34 项通过；锁定上游的原世界扫描循环在同一夹具中触发 13 项失败。覆盖媒体／已知模型索引不同、默认 brush 占位、BSP／子模型、高索引、普通服、实际实体加载及退役 |

F-001：status=validated，severity=n/a_re，confidence=high，依据 E-001～E-004。Renderer 在过滤 brush 前调用会加载的原生 getter，抵消了人物延迟登记。`deferredModels` 只证明登记过，不能证明仍保持延迟。

P-001，path_type=callflow：SHA 清单通过 → 原人物登记分支 → Renderer GL_BuildLightmaps 查询全部服务器媒体 → getter 对 needload 1／2 同步调用 Mod_LoadModel → Studio 校验／解码／纹理加载。此链由 E-001～E-004 支持；新的源码截断建图扫描对延迟人物的强制加载，不修改实体实际使用路径。

原始采样已无损压缩到 `cross-lostcity.json.raw.gz`：172,707,232 字节变为 19,264,781 字节，解压后完整长度和 SHA 核同后才删除未压缩副本。回执为 `raw-compression.json`；未删其他任务证据。

## 源码改动

`native/include/goldcraft/precache_api.h` 新增独立 `GoldCraftModelScanAPI`；旧版已知模型接口保持 ABI。新接口按服务器媒体索引查询，返回停止、跳过、已加载 brush 或需要原生 getter，仅读当前连接内存，不分配、不读盘、不调用 GL。

`native/client/precache_client.cpp` 在 owner 线程、当前令牌、非接收／退役状态下检查当前媒体表。不能用未加载模型的默认 type 分类。仅 SHA 已验证扩展连接中的延迟人物路径跳过；其他待加载条目交回原生 getter，普通服务器原生懒加载也保留。状态增加三项冷扫描计数，随连接重置。

Renderer 的 `gl_rmain.cpp` 增加共用枚举器，并将 `GL_BuildLightmaps`、`R_LoadWorldResources`、`R_Reload_f` 接入。旧提供者／无接口保留原查询循环；新接口一旦判定陈旧或退役就停止，不能回退读取旧表。实际实体的 `model_for_index` 和原加载流程保持。

这是宿主／Renderer 适配，不宣称是原 CSOL 源码结构。SHA、最终 VFS、CRC、RLE／序列检查、纹理、效果和灾变 AI 均不以性能为由削减。

## 复核方法与剩余工作

Git Bash 下按 [夹具说明](../tests/renderer-model-scan/README.md) 运行实际代码测试；`--old-scan` 要求检出旧问题。回执封存编译输入及实际头文件。最初夹具名称冲突和缺少实际退役对象的链接失败保留，未改生产行为来通过测试。

完整构建准备首次存在重复解析 Ninja 头路径的额外开销，已按实际文件名先去重；首轮不作为可部署成品。后续稳定构建、匹配符号、完整 HUD 闭包、配套方案和发布记录另附。

新的四文件配套需包含 GoldCraft／Renderer 的 DLL 与匹配 PDB，连接测试服的权威下载资源也需一致。尚未更新本地主服／B 或正式服。新的物理 Escape 停止仍有效；待完整方案准备好后再恢复 GUI。还需双向跨图 CPU／IO、原生进度、窗口响应、BSP／门和地图资源、首次人物实际显示、多分辨率、公网及多人。完整灾变和自定义玩法目标保持开放。
