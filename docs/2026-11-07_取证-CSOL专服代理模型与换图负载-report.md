# CSOL 专服代理模型与换图负载

2026-11-07。原版把网络使用的模型名称与专服计算使用的模型指针分开：角色表仍完整注册，dedicated 路径会将多数模型映射到共用或专用的服务器模型，再查询/加载缓存。当前移植的角色文件则含完整动画，并由 ReHLDS 按原路径实际载入。这是已证实的加载架构差异，不是已测得的原版换图速度对照。本轮仅取证与文档，没有生产代码、编译、部署或 Git index 改动。

## 样本和可复核材料

| 模块 | PE / 基址 | SHA-256 |
|---|---|---|
| hw.dll | x86 PE32 / `0x37100000` | `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783` |
| mp.dll | x86 PE32 / `0x10000000` | `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2` |

私有样本位于 `analysis/map-transition-active-20261107/original/ida/`。`original-cache-capacity-20261107` 与 `original-characters-20261107` 为本轮 IDA 会话；其旧输入路径在用户清理 `build/` 后失效，但存活会话读取的原字节已导出，再与以上保留的同 SHA 样本比较。未重建被清理的 source/build 副本。

| 证据 | 相对 analysis/map-transition-active-20261107/original 的路径 | SHA-256 |
|---|---|---|
| E01 原 IDA 读取 | `character-evidence-v2.json` | `5e9974ad7c9a2a711bb3ebcbea1876cbee2b5128216dcf58feffc1a25a569ca5` |
| E02 PE 对照与独立解码 | `character-verified.json` | `d1f2ec3fe736c8fde3d72713c752a7c426fabc3c04fadcad8c28b0afec862534` |
| E03 当前资源目录/索引 | `dedicated-resources.json` | `bae8078dea5a85d60beb1baedfba63df4bdcfd2f6214f687b74ab0415c6f06bd` |
| E04 角色输入/派生文件清单 | `character-corpus.json` | `9029786be8300c0558e6e38999837c8f44da6d1a5383c598e2de0754e2904ccd` |

E02 共核对 53 段、23,393 字节；范围含重叠，不能当作独立代码总量。其中 23,389 字节来自 PE 文件，`0x38403268` 的 4 字节为映像虚拟零填充，明确单列 `zeroFillBytes`，不冒称磁盘字节。原字节比较全部通过，静态角色数和六个实际引擎表槽检查通过。部分读取为数据或在指令中途结束，`decodedBytes` 如实记录；不声称 53 段均是完整函数。

复核命令（项目根目录，Python 需有 Capstone）：

```powershell
python analysis/map-transition-active-20261107/original/verify_characters.py
```

核验脚本 SHA-256 为 `6896b74aab2fb5aabf5c43d8c4e4837b1ba0c019011038276bd09345aba30c52`。E03 记录归档尺寸、mtime、条目和匹配结果，没有逐个读取全部归档 payload 或声称已核其完整 SHA；E04 则读取并记录每个原模型、SEQ、派生模型正文 SHA。材料为本地分析，不将原模块、反汇编或游戏资源发布到源码仓库。

## Evidence → Finding → Path

### F01：完整注册与专服实际加载分离（E01、E02，validated）

MP 预缓存函数起点 `0x11489940`（RVA `0x1489940`）。`0x1148A0B5–0x1148A0F4` 遍历 `0x11DA3A20` 的 163 个路径，数量来自 `0x11DA3A1C`，逐项调用引擎槽 `0x122785A4`。随后调用 `0x122787E0`，传入 type 2 和 `(-38,-24,-41)` / `(38,24,41)` 范围。`0x1148A0F6` 后另注册两个角色，`0x1148A121–0x1148A142` 还遍历动态角色槽 100–199。不能说原版只预缓存当前在线人物。

闭合 ABI：MP `0x114B22B0` 复制传入引擎表的 `0xD5` 个 DWORD 到 `0x122785A0`；hw `0x3793ECC9–0x3793ECD3` 向 GiveFnptrsToDll 传入 `0x383099F0`。该表 `+4` 是 `PF_precache_model 0x378C7AA0`（RVA `0x7C7AA0`），`+0x240` 是 `ForceUnmodified 0x378C5870`，后者记录一致性检查，不能当成第二次模型加载。CSO 引擎表扩展后与普通 SDK 后段偏移不同，本报告使用实际表和消费者核定槽位。

`PF_precache_model` 以原路径写入 `0x39684DB0[index]`。在 `0x378C7B7E` 检查 dedicated 标志 `0x39414C70`：

- 专服：经 `0x378C8CA0` 解析实际路径，然后在 `0x378C7B95` 调 `Mod_ForName 0x3787D580(mapped,1,1)`，结果指针写入 `0x3968ADB0[index]`。
- 非专服：调用 `0x3787D180(1,original)` 查找/建立模型记录、设置 needload；这个函数本身不读取 MDL 正文，不能误称为同样的同步加载。

`Mod_ForName` 先经 `0x3787D180` 查同名记录，再到 `0x3787F7E0` 实际加载。`0x3787F866–0x3787F89D` 的缓存命中分支不重读文件；映射到相同 dedicated 路径的多个网络模型可复用模型记录和缓存。

### F02：原路径解析有明确顺序和例外（E01、E02，validated；两项上层来源待查）

`0x378C8CA0`（RVA `0x7C8CA0`）顺序如下。子串匹配经 `0x37F53CA8 → IAT 0x3800FBB8` 的 CRT `strstr`，区分大小写；不应擅自改成大小写无关匹配。

| 顺序 / 条件 | 实际路径或结果 |
|---|---|
| override 标志 `0x38403268` 非零 | `0x3767EAD0` 将 `0x382F0EA0` 的全局字符串复制为输出；谁为每种模型设置它尚未完整审计 |
| 包含 `/v_`、`/w_`、`/p_`、`/d_` | 分别为 `models/v_dediweapon.mdl`、`w_dediweapon.mdl`、`p_dediweapon.mdl`、`d_dediweapon.mdl` |
| 包含 `models/monster/` | `models/monster/dedi-%s`，尾部取输入 +15 |
| 包含 `zombi_` | 含 `_trans` 保持原样；否则在其后的首个 `/` 截断并接 `-dedi.mdl`；找不到 `/` 时保持原样 |
| `0x37A6AB30(original,out)` 返回 true | 使用该配置映射结果；它遍历 `0x39413D28` 对象的 `+0x1B88/+0x1B8C`，完整配置来源/优先级未闭合 |
| 前缀 `models/player/snowman_` | `models/player/snowman-dedi.mdl` |
| 包含 `models/player/z4_` 或 `models/player/zg_` | 移除扩展名后接 `-dedi.mdl` |
| 包含 `models/player/player-10up` | 保持原样 |
| 其他 `models/player` | 多个女性原路径子串命中则为 `models/player/woman_dedi.mdl`，否则 `models/player.mdl` |
| 包含 `/costume` | `models/null.mdl` |
| 其余 | 保持原样 |

女性分支 `0x378C949E` 取字符串 `0x380A97CC`；默认男性分支 `0x378C9482–0x378C949C` 取 `0x3809A740`。女性条件包括 `/ChoiJiYoon`、`/Criss`、`/Jennifer`、`/Natasha`、`/ma_alice`、`/buffclassa`、`/buffclass26s2ct` 等。当前 `csolNNN` 别名已失去这些名称信息；映射时不能拿别名直接套原字符串条件，必须保留真实 sourceModel 身份及原例外。

### F03：代理模型参与权威计算，不能任意代替（E01、E02，validated）

| 原版消费者 | VA（RVA） | 实际使用的内容 |
|---|---|---|
| PF_SetModel | `0x378C7DF0`（`0x7C7DF0`） | 原路径先取得网络 index；`0x378C7E36` 用 index 读取 `0x3968ADB0` 的实际模型指针，再读取 bounds |
| GetModelPtr | `0x378CC130`（`0x7CC130`） | 同一实际模型指针经 `Mod_Extradata 0x3787D0A0` 返回模型数据 |
| GetBonePosition | `0x378E36B0`（`0x7E36B0`） | 实际模型及实体 sequence/frame/controllers 交给 studio setup-bones 回调 |
| GetAttachment | `0x378E35B0`（`0x7E35B0`） | 同一模型的 attachmentindex `+0xD8`、88 字节附件记录的骨骼和位置，再计算骨骼 |
| 实体碰撞入口 | `0x378EC120`（`0x7EC120`） | `0x378EC347–0x378EC378` 读取实际模型/序列；后续把它交给 hull 生成 |
| Studio hull | `0x378E95B0`（`0x7E95B0`） | `0x378E9866–0x378E9897` setup bones；`0x378E98A6/0x378E98B1` 读取 hitboxindex `+0xA0` / numhitboxes `+0x9C` |

MP `0x114ED6A0` 中的 SetModel 引擎槽与上述 PF_SetModel 闭合。三个骨骼消费者调用的实际回调来自 `[0x3830779C]+4`。引擎默认 `0x378DF210` 在序列越界时回退 0，但 **MP 可以提供 studio blending API；当前没有完成实际回调选择/MP 实现的审计**，不能据默认函数推出专服总会回退到 0，也不能据此任意替换序列不匹配的模型。

P01：MP 全表 precache → 原名称/网络 index → dedicated 路径解析 → 映射名查缓存/加载 → 保存实际模型指针 → SetModel / bone / attachment / hull 消费。减少的是实际模型集合及读取工作量；原名称及客户端完整视觉模型仍保留。

## 当前资源和负载（E03、E04）

只读扫描了现有 `G:/New NARTOOL/CsolTools` 松散目录、`H:/Counter-Strike Online/Data` 的 6 个 NAR 及 Packer 下 23 个 `models*.pak` 当前索引。合计 29 个归档、47,909 条目、19 个归档匹配、33 个松散匹配、0 读取错误。扫描没有提取资源或替换 common；不是凭旧索引猜缺失。

扫描范围内没有 `woman_dedi.mdl`，也没有 `v/w/p/d_dediweapon.mdl`。存在少量怪物 dedicated MDL，以及 `woman_dedi1.seq`、`woman_dedi3.seq`、`woman_dedi11.seq`；SEQ 的存在不能代替缺失的 MDL。

另有同名资源冲突：common 的 `models/player.mdl` 为 281,324 字节、77 个 sequence（SHA `fb59b69ff909b5e1e8d22616d4ffdaa24deb40f698fc8711cfc63487a35582cd`），大小对应 valve.nar 条目；cstrike.nar 的同名条目为 1,468,932 字节，estrike.nar 为 1,468,372 字节。尚未从原挂载顺序与实际读取闭合它们的选择，不能把 common 文件直接宣称为正确 CSO 服务器代理模型。

当前角色目录 151 个条目对应 **150 个唯一 alias / 150 个存在的派生 MDL**。这是本轮逐项核验数量；不能把未复核的“170 个”写成角色 alias 总数。

| 对应这 150 个 alias 的内容 | 字节 |
|---|---:|
| 原始主 MDL | 328,817,800 |
| 原始外置 SEQ | 645,861,708 |
| 原始 MDL + SEQ | 974,679,508 |
| 当前派生 MDL | 1,229,174,960 |
| 当前派生非像素前缀 | 1,171,571,524 |

原始可解析 IDST 前缀合计 267,271,584 字节，但 csol28/csol32 原头不直接为 IDST，此项为部分统计，不能用于完整比例。派生 sequence 数分布为 353/360/361/367。`tools/Prepare-CSOLCharacters.py` 为每个 alias 收集全部 SEQ 并调用 `csol_character_pack --extended` 生成单独模型；目前不能仅凭体积增大推断重复动画 bug。

当前宿主对应路径 `external/ReHLDS/rehlds/engine/pr_cmds.cpp::PF_precache_model_I_internal` 直接调用 `gc_precache.AddModel(s, Mod_ForName(s, TRUE, TRUE), ...)`。与原版 dedicated 映射的差异已在实际源码核验。原版非像素缓存与独立固定 cache 的既有证据见另两份 2026-11-07 取证报告；当前150个角色的前缀仍约1.17GB，所以只去掉像素不等于解决动画模型加载量。该尺寸本身也不等于进程峰值常驻内存或实测耗时。

## 边界与下一步

原版专服代理机制已经闭合到实际加载和碰撞/骨骼消费者；两项可配置路径的生产者、服务器 blending 回调以及缺失代理资源仍未闭合。应继续查清这些边界，不能跳过映射例外、删除动画或使用无匹配 hitbox 的模型来制造提速。

原版实际换图时间没有采集，本轮没有客户端卡顿帧样本。父任务报告的宿主 prefix 候选 10.377 / 11.025 秒，未优于此前 7.877 秒，只能表明那一候选未获得性能验收；这不是本子任务独立实测，也不是原版/移植的严格 A/B 对照。生产修复、完整构建、相同地图计时、单 B 进图响应及动作/命中验收仍由父任务继续。

共享状态建议交父任务维护：原版已证“全表注册、专服模型指针替换、共用缓存”，现有150个角色派生体积及缺失代理资源已登记；暂不能照搬任意代理。保留换图性能与客户端无响应未完成项。证据文件位于 analysis，不再依赖已清理的 build 目录。
