# CSOL 模型缓存与换图清理：原版证据

2026-11-07，本轮为只读取证，不含生产代码修改、编译或部署。范围是用户授权的 CSOL 引擎工作区副本；没有访问受保护安装或操作游戏。

清理后复核说明：下文 `build/map-transition-stall-20261106/original-cache-audit/` 原材料已随用户授权清理删除，其统计和命令仅保留为历史记录，不声称文件仍存在。本轮从存活 IDA 会话重读核心分配、复制、SEQ、清理及 cache 边界块，并与同 SHA 保留样本核对；现存材料为 `analysis/map-transition-active-20261107/original/character-evidence-v2.json` / `character-verified.json`，复核命令为 `python analysis/map-transition-active-20261107/original/verify_characters.py`。新材料并非已删除材料的逐字节备份，读取覆盖以其53段清单为准；完整身份和限制见[专服代理模型报告](2026-11-07_取证-CSOL专服代理模型与换图负载-report.md)。

原样本为 hw.dll，PE32 x86，映像基址 0x37100000，SHA-256 **c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783**。私有 IDA 会话 original-map-cache-private-20261107 指向 build/map-transition-stall-20261106/original-cache-audit/ida/hw.dll。本文地址均为 VA；对应 RVA 已记录于 verified-evidence.json。IDA 现有库仅少量函数已定义，部分反编译有栈平衡警告，以下结论以完整原字节及指令为准。

| 证据 | 原版位置及已证事实 | 实现相关位置 |
|---|---|---|
| E01 | CSO_LoadStudioModel 0x378B0870，在 0x378B08EC 比较头 +0xB8（textureindex）；为零取 +0x48（length），非零取 +0xBC（texturedataindex）。0x378B090D 以这个长度调用 Cache_Alloc 0x3798AE70，owner 为 model+0x200。 | external/ReHLDS/rehlds/engine/l_studio.cpp::Mod_LoadStudioModel_internal 当前使用 length+1280*numtextures，缓存全图像并扩展调色板，和原版不同。 |
| E02 | 同函数 0x378B0D46–0x378B0D4D 从同一个局部变量取长度，调用 0x37866520（Q_memcpy 行为）将输入头前缀复制到刚分配的 cache.data。0x37866520 原字节已核，含逐字节/逐 DWORD 拷贝与 CRT 快路；不是清零、延迟复制或纹理指针替换。 | 可用相同“非像素前缀”语义减少专服模型缓存。不得删除骨骼、hitbox、事件或动画。 |
| E03 | 纹理处理 0x378B03D0，在 0x378B03E3 检查 dword_39414C70；非零于 0x378B0405 直接跳 0x378B0851 返回，跳过整个纹理循环/上传。此标志也控制 0x378B0955 的顶点处理及头骨/材质客户端数据。 | 专服无需上传/保留像素，但宿主其他消费者必须另核；不能据此改客户端纹理流程。 |
| E04 | 标志来源：0x37942200 启动方法在 0x37942213 取第一个 bool 参数并作为第四参数传给 0x37940FC0；后者在 0x37940FEB 读取 [ebp+0x14]，0x37940FFF 写 dword_39414C70。0x3767C688 非零分支选择 models/vxl_dedi/vxl_door.mdl 等专服模型，失败路径 0x3767CA95 使用原字符串 CVoxelProp::Precache::g_bIsDedicatedServer(%s) fail。0x37941DF7 同标志还选择 80 MiB 专服和 256 MiB 非专服缓存上界。这几处独立消费者支持该字段为 dedicated flag。 | 与当前 ReHLDS g_bIsDedicatedServer 角色对应，不需要硬编码原版全局地址。 |
| E05 | 外置 SEQ：CSO_LoadModel 0x3787FCD6 后先计算新的 retained length=原 texturedataindex + 原 sequence descriptor bytes + 各 SEQ 文件长度；0x3787FDD6 写 length。先调用纹理处理，0x3787FE33 临时将 textureindex 置零，0x3787FE3D 合并 SEQ，0x3787FE4D 调 LoadStudioModel，0x3787FE6D 在缓存头恢复 textureindex。因此该路径按已整理的 length 缓存动画前缀，没有复制原像素尾。 | native/csol/sequences.cpp 的 AppendSequences 已把全部动画、描述符与 safety 元数据放在新的 texturedataindex 前，像素尾在后；native/csol/sequence_owners.hpp::PreparedSequenceModel::RetainedBytes 使用这个边界。父任务另做安全/消费者审查。 |
| E06 | 快路：CSO_LoadModel 0x3787F82C 类型2/3调用 Cache_Check 0x3798B0A0，命中后重置 needload 并直接返回。Mod_Extradata 0x3787D0A0 同样先检查 model+0x200，只有未命中才调用 LoadModel。 | “同一次加载重复查询可以命中”有原证；不意味着跨图全保留。 |
| E07 | 清理：0x3789F7E0 含 Clearing memory；0x3789F82D 调 0x3787D070，仅标记 needload，不直接释放；随后 0x3789F841 调 0x3798B530。后者遍历 cache，0x3798B558 传入 .mdl 字符串，0x3798B55E 调 0x37866910→0x37F53CA8→IAT 0x3800FBB8。IDA imports 和 PE 表均确认此为 VCRUNTIME140 strstr。匹配成功时摘除 cache 链/LRU 并在 0x3798B5BB 清 owner.data；不匹配则跳过。 | **原版该内存清理路径确实释放名字包含 .mdl 的 cache**。不能把双快路推论成原版永远跨图保留 MDL。 |

F01（n/a_re，validated，高置信度，E01/E02/E03/E04/E05）：原版模型缓存保留经处理的非像素前缀；dedicated 分支跳过纹理处理。优势是减少同地图工作集和无用像素处理，而不是简单增大预算。

F02（n/a_re，validated，高置信度，E06/E07）：命中快路与跨图生命周期必须分开。上面的清理函数有模式守卫，不能声称所有换图分支均同样清理；但“原版所有地图都保留 MDL”已被这条真实路径否定。原版完全换图耗时仍未实测，不给未经测量的提速比例。

P01（callflow）：LoadModel → SEQ 合并（如存在）→ LoadStudioModel → Cache_Alloc(prefix) → dedicated 跳过纹理消费 → memcpy(prefix) → Mod_Extradata/LoadModel 的后续 cache hit。

P02（callflow）：内存清理 3789F7E0 → 标 needload 3787D070 → 按 .mdl 名字筛选的 cache 清理 3798B530 → 后续 load 只能重新载入被清掉的条目。

历史证据目录为 build/map-transition-stall-20261106/original-cache-audit/。当时 raw-ida-blocks.json 保存 19 份 IDA 原字节和 imports 证据；verify_evidence.py 从确切 PE 文件逐块核对，verified-evidence.json 记录 19/19、共 6,895 字节一致，每块的 VA/RVA/SHA 和独立反汇编。ida-evidence-02.json 当时保存反编译/反汇编和调用引用。以下为清理前历史命令，现已不能执行；当前复核入口见本文开头：

```powershell
python build/map-transition-stall-20261106/original-cache-audit/verify_evidence.py
```

时序：先复核样本 SHA；再核分配/复制与 dedicated 消费者；随后沿 Mod_ClearAll 后续调用发现 .mdl 释放；最后核源序列布局并保存全部原字节。这是原版静态控制流验证，没有新增游戏性能/画面结论。

共享状态精确建议（交父任务维护，不自行覆盖四共享文档）：换图优化已获得原 CSOL 非像素缓存与专服跳过纹理的 IDA 证据；跨图 MDL 不得声称无条件保留。下一步完成宿主消费者/边界回归和相同地图加载计时，再决定部署；本报告不把候选当作已提速交付。

