# CSOL 专服骨骼回调与代理资源边界

2026-11-07，延续[专服代理模型与换图负载](2026-11-07_取证-CSOL专服代理模型与换图负载-report.md)。原版 MP 的 `Server_GetBlendingInterface` 在引擎请求版本 1 时必定返回成功并安装 MP 自己的骨骼回调；没有 dedicated 分支阻止接管。这个回调使用传入代理模型中的数值动作索引，负数或越界才回退 0。缺失的代理模型仍不能用任意简化模型代替。本阶段只读分析，不改生产代码、构建、部署、运行状态或 Git index。

## 身份和核验

沿用私有工作区样本 `analysis/map-transition-active-20261107/original/ida/`：

- hw.dll：PE32 x86，基址 `0x37100000`，SHA-256 `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`。
- mp.dll：PE32 x86，基址 `0x10000000`，SHA-256 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`。

继续使用已有两个 IDA 会话，未重开模块或重新验证前一轮 53 段。IDA 对新函数反编译失败、自动 xref 为空，因此以 IDA 原字节和固定 SHA PE 的独立解码为准；未把空 xref 当成没有调用。

证据目录为 `analysis/map-transition-active-20261107/original/`：

| 证据 | 文件 | SHA-256 |
|---|---|---|
| E01 新 IDA 读取 | blending-evidence.json | `6db1f5e58325321048f5909004bcdd3b9772cadb314c19db2379191d39d3e182` |
| E02 文件对照/导入导出/指令 | blending-verified.json | `d45cf99e28499923f7a0294befe6ae5cc847a5cab9e5c0855c6c3bedaebb26cf` |
| E03 原 NAR player.mdl 头及标签 | player-proxy-archive-headers.json | `c87786e480a7df3a6bc7a4ab56df7855fd4f60fb4f87ba95a2ea0b22520ab065` |
| E04 其余 PAK 当前索引 | dedicated-resources-other-pak.json | `8500af0ddf8010003be832a66fc4a6e89905b7457bcd78031471fc8d11000d91` |

E02 核对新增 12 段 / 6,747 字节全部与 PE 文件一致，7 项表、导入、导出断言通过。包含完整 setup-bones 函数 `0x113F8E00–0x113FA3C0`（末尾 ret 单独读取），也包含表数据及邻接指令；不把每一段都说成完整函数。验证入口：

```powershell
python analysis/map-transition-active-20261107/original/verify_blending.py
```

验证脚本 SHA-256 `4bee326fe303d0d2924a6133b862136c27a3c72e359e4a5c6f3cdbfafda0fd82`。该脚本只核验原始证据和静态契约，不执行原游戏、不声称已做命中盒实机测试。

## Evidence → Finding → Path

### F01：匹配 MP 的正常握手会替换默认回调（E01、E02，validated）

hw `0x378A6037` 及独立函数 `0x378AAC60` 先调用 `0x378E7920`，将 `0x3830779C` 恢复为默认接口 `0x383079A0`；默认表内容是 `{1, 0x378DF210}`。随后遍历已加载扩展模块，查询 `Server_GetBlendingInterface` 字符串 `0x380A5CD0`。原 PE 导入表确认函数指针槽 `0x3800F4D4` 为 `KERNEL32.dll!GetProcAddress`。

hw `0x378A605F–0x378A6075`（独立路径 `0x378AAC8D–0x378AACA3`）依次传入：版本 1、接口指针地址 `0x3830779C`、server studio API `0x38307990`、rotationmatrix `0x39A57BE0`、bonetransform `0x39A563E0`。返回非零立即结束遍历；返回零打印版本不匹配并恢复默认。

MP 导出表确认 `Server_GetBlendingInterface` 为 `0x113FBC00`（RVA `0x13FBC00`）：

- `0x113FBC03` 比较版本 1，只有不匹配才返回 0。
- `0x113FBC10` 将输出接口写为 `0x11DA1258`，表值为 `{1, 0x113F8E00}`。
- `0x113FBC19/0x113FBC31` 复制传入 API 的 4 个指针至 `0x1222C9D4`；其第 4 项来自 hw `0x3830799C = Mod_Extradata 0x3787D0A0`。
- `0x113FBC1F/0x113FBC27` 保存两套输出矩阵地址，随后无条件返回 1。

因此该匹配 MP 被正常查询时成功安装 `0x113F8E00`（RVA `0x13F8E00`）；不能继续把 hw 默认 `0x378DF210` 当成这条成功路径的实际算法。此结论为两个模块的静态成功握手闭环，未读取正在运行的原版扩展模块顺序；若更早的其他模块返回成功，遍历会提前退出，这项条件如实保留。

### F02：直接消费代理模型的序列；没有按名称自动适配（E01、E02，validated）

MP setup-bones `0x113F8E00` 的第 1 参数是引擎传入模型，第 3 参数为 sequence。`0x113F8E3D` 经保存的 Mod_Extradata 取该模型头；`0x113F8E4E–0x113F8E62` 检查 `0 <= sequence < header.numseq(+0xA4)`，否则将本地 sequence 置 0。接着用 `header.seqindex(+0xA8) + sequence*176` 选择描述符，并用同一头的 boneindex `+0x90`。

动画数据助手 `0x113FB9E0`（RVA `0x13FB9E0`）读取描述符 seqgroup `+0x9C`：

- group 0 返回当前模型头加 animindex `+0x7C`。
- 非零 group 从当前模型 `+0xC0` 的缓存数组查询，未载入时按该模型 seqgroup 表中的名称调用引擎 LoadCacheFile；再加 animindex。

这不是按客户端完整模型名称重找动作的分支，没有把不兼容的代理动作表自动变成正确表。正确的代理仍需骨骼/动作/hitbox 数据匹配。

同一个函数还存在必须保留的骨骼行为：

- `0x113F9056` 检查 `numblends == 9`，从实体私有状态读取两轴混合；普通多混合序列另用 blending byte。
- `0x113F99B5` 后，9 路混合在 sequence 101–159、198–207、360、8、9 时跳过 gait 叠加，其余读取 `0x114250C0` 给出的 gait sequence，仍用当前模型数值索引并检查范围。
- gait 合并按当前模型骨骼名分界，字符串在 `0x119C1690/0x119C169C`；不是通用的“取全部第二套骨骼”。
- `0x113F9C3B–0x113F9D03` 使用实体 `+0x1D8/+0x1DC/+0x1E0` 进行每轴缩放，再写入引擎提供的矩阵。

上述分支用于说明任意代理替换的正确性边界，尚未完整移植或逐数值对照这些骨骼算法。没有将未知辅助字段命名为已确认的网络结构。

P01：引擎恢复默认 → 查询 MP 导出 → version 1 成功握手 → 安装 MP setup-bones → 引擎传入映射后的模型 → Mod_Extradata → 原 sequence 直接索引该模型 → 混合/gait/矩阵 → 引擎继续读取该模型 hitbox。上一报告的 GetAttachment / GetBonePosition / hull 三个消费者由此接入同一 MP 回调。

## 资源缺口和已排除的来源（E03、E04）

在之前 6 NAR + 23 models PAK 的 47,909 条目扫描基础上，本轮只补扫其余 99 个当前 PAK 索引：27,526 条目、0 新 dedicated 匹配、0 读取错误。合起来覆盖当前 **6 NAR + 122 PAK / 75,435 条目**，仍未找到 `woman_dedi.mdl` 和四种 `v/w/p/d_dediweapon.mdl`。这里只读取索引，无全库提取或覆盖 common。

原 `models/player.mdl` 的三个 NAR 版本已在内存中解码，验证存储 CRC、解码尺寸并计算内容 SHA；没有写出/部署模型：

| 来源 | 大小 | 骨骼 / hitbox / 主文件 sequence | SHA-256 |
|---|---:|---|---|
| cstrike.nar | 1,468,932 | 46 / 21 / 111 | `eb147c4cdb8a42422cfb5140ad5a8fd4b154540f76ac893128cd17f7e5fb39da` |
| estrike.nar | 1,468,372 | 46 / 21 / 111 | `0cc96f08fb1b394853f54388153284ef9be6129a1b45ae5949104d648b8316bb` |
| valve.nar | 281,324 | 50 / 20 / 77 | `fb59b69ff909b5e1e8d22616d4ffdaa24deb40f698fc8711cfc63487a35582cd` |

common 当前文件 SHA 与 valve 版本完全相同，现已不只是大小相同。common 另有 13 个 playerN.seq，共242个序列、4,308,028字节；数值上 `111+242=353`，仅为文件计数，不能单靠相等证明与150个派生角色的骨骼、动作顺序及命中盒兼容。女性仅找到 woman_dedi1/3/11.seq，基础 MDL 仍缺失。

既有本地资源选择报告（2026-10-24，未公开）曾记录挂载优先级，但其 build 原证已删除，本轮没有重新核原挂载初始化/实际读取，故不据历史文字直接选定代理正文。也未发现缺失 dedicated MDL 的生成函数，不宣称客户端包必然包含它们或引擎会自行生成。

资源复核入口为 `inspect_player_archives.py`（只解码已扫描条目到内存）、`scan_other_paks.py`（只扫描未覆盖 PAK）；全部在上述 analysis 目录。`player-proxy-sequences.json` 保存现有SEQ计数及完整SHA。

本轮已关闭“默认还是 MP 骨骼回调”的静态握手问题。剩余为：专服实际代理资源及挂载来源、override/配置映射的上层生产者、正确代理与当前派生动作表的兼容性。父任务仍需在宿主优化后测同图/跨图耗时、单 B 进图响应及人物动作/命中；本报告没有性能验收数字或部署结论。
