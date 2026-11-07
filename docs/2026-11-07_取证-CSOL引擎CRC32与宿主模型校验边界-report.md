# CSOL 引擎 CRC32 与宿主模型校验边界

2026-11-07。原 hw 的公开 CRC 接口使用 reflected IEEE CRC32，多项式 `0xEDB88320`，原字节和指令回放已核验。父任务拟优化的是 ReHLDS 现有完整文件 CRC 的计算速度；这属于保持结果和生命周期的宿主适配，不应宣称原 CSOL 已采用 slicing-by-8，也不能借性能优化省略校验。

本轮只有取证脚本和文档，没有生产代码、编译、部署、Git index 或运行环境修改。原版完整模型加载路径没有找到与 ReHLDS 同位置的逐文件 CRC/modified 拒绝段，下面分别记录原版算法和宿主现有规则，避免把不同实现混为同一证据。

## 身份、Evidence 与验证

原 `hw.dll` 为 PE32 x86，基址 `0x37100000`，SHA-256 `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`，位于 `analysis/map-transition-active-20261107/original/ida/hw.dll`。继续使用现有 IDA 会话 `original-cache-capacity-20261107`，没有重建源码副本或重新核前两轮片段。

材料均在 `analysis/map-transition-active-20261107/original/`：

| 证据 | 文件 | SHA-256 |
|---|---|---|
| E01 IDA 读取 | crc-evidence.json | `71c6b80446336d44504936711f10c84af48e81e6d50fcb6b1fa7b19f09e76abc` |
| E02 PE核对、指令、回放结果 | crc-verified.json | `51251a3580ff79be0dccddc21f3346be9549f0ff508cdd4179cd3900c5c5571a` |
| E03 核验/原指令回放脚本 | verify_crc.py | `27ad83c4fa09709b380653185840e802174e1e530e2e5cd28e907a6cfe4c08e1` |

9 段 / 3,694 字节与固定 SHA 的 PE 文件完全相符，256 项表与 IEEE 多项式独立生成的结果完全相符。Unicorn 执行原函数指令，以 zlib 作结果对照：1,104 组长度、对齐和任意初值通过，161 组分段调用等价通过；65,101 次输入读取均未越过该次传入区间。标准向量 `123456789 → CBF43926` 通过，Init/Final 与引擎表槽检查通过。负长度没有交给原版回放；不推断原版会安全接受负长度。

复核入口（Python 需有 Capstone、Unicorn）：

```powershell
python analysis/map-transition-active-20261107/original/verify_crc.py
```

此为原指令算法对照，不是新版宿主编译结果或实机提速验收。脚本仅记录核验时宿主源码 SHA，不冻结或复制第三方源码树。

## Findings 与调用路径

### F01：原 CRC 是 IEEE，并支持未取反状态的续算（E01、E02，validated）

| 原接口 | VA（RVA） | 关键行为 |
|---|---|---|
| CRC32_Init | `0x37867EE0`（`0x767EE0`） | 将输出 DWORD 写为 `FFFFFFFF` |
| CRC32_ProcessBuffer | `0x37867EF0`（`0x767EF0`） | 读取传入状态，按指定地址及长度更新，写回未取反状态 |
| CRC32_ProcessByte | `0x378680C0`（`0x7680C0`） | 单字节更新，同一 IEEE 表 |
| CRC32_Final | `0x37867ED0`（`0x767ED0`） | 返回状态逐位取反 |
| CRC 表 | `0x3809F170`（`0xF9F170`） | 256 DWORD，全表与 `EDB88320` 生成值相同 |

原引擎函数表 `0x38309B5C..0x38309B68` 依次指向 Init、ProcessBuffer、ProcessByte、Final，接口身份已由实际指针闭合。

ProcessBuffer 的字节递推为 `table[(state ^ byte) & 255] ^ (state >> 8)`。`0x37867F65–0x37867FDC` 已按每轮 8 字节展开，以两个 DWORD XOR 和连续的单表查找实现；不是八张表并行的 slicing-by-8，也没有使用 CRC32C 指令。余数由 `0x3786809C` 的 8 项跳转表处理，0 长度不读取输入。父任务可以在宿主实现等价八表版本，但必须保留任意初值、分段续算、精确输入范围和最终取反语义。

P01：原引擎表 → Init(FFFFFFFF) → ProcessBuffer 精确区间，可多次续算 → Final(~state)。硬件 x86 `crc32` 指令计算 Castagnoli 多项式，不能直接替换这一接口。

### F02：原模型 CRC 元数据存在，但加载时首次记录链未闭合（E01、E02，partial）

`Mod_FindName` 形态函数 `0x3787D180` 在 `0x3787D2C5–0x3787D2E4` 为新模型记录 12 字节信息：第一 DWORD 取调用者参数，其后两个 DWORD 清零。`0x37882150`（RVA `0x782150`）按模型名查询记录，在 `0x3788217C` 检查中间 DWORD：非零时比较传入值与第三 DWORD，不等返回 0；首次标志为零或值相同返回 1。其行为与 shouldCRC / firstCRCDone / initialCRC 的元数据形状相符。

但是，已读取的完整 `0x3787F7E0–0x3787FFA3` 模型加载函数中，没有直接调用这四个 CRC 接口；在获取文件数据/长度后直接进入格式分发，未见 ReHLDS 的同位 first-CRC 写入和 `has been modified since starting the engine` 报错段。这里不能把元数据存在推断为原加载路径必定计算整个 MDL，也不能把“没有直接调用”扩大为它的所有下层文件系统均不作完整性验证。首次写入、间接/其他校验路径仍未知，本轮不继续追长链。

P02（已闭合部分）：Mod_ForName `0x3787D580` → 名称记录 `0x3787D180` → CSO_LoadModel `0x3787F7E0`；独立的 `0x37882150` 消费元数据比较。尚未找到把“完整文件 CRC 结果”写入这些元数据并与上述主加载函数闭合的生产者。

### F03：本次优化必须保留宿主已存在的完整文件规则（实际源码）

核验时宿主 `external/ReHLDS/rehlds/engine/model.cpp` SHA-256 为 `2e1a514494b29f86b1377d33fa3f9cd61b5afb92fdce993ff9f630e146cc6f13`；`crc.cpp` 为 `a07690b00cc9edcdeccf4ce9fa5ebbf6bb3e066cd9d9e4490c67746041e3e98b`。父任务后续源码变化不由这些旧 SHA 自动背书。

实际 `Mod_LoadModel` 当前顺序为：

1. 存在模型缓存命中则沿既有返回路径退出；真正重新加载时 `COM_LoadFileForMe(mod->name, &length)` 取得正文。
2. `trackCRC && GC_ModelCRC(mod)->shouldCRC` 成立时，Init → `CRC32_ProcessBuffer(&currentCRC, buf, length)` → Final。输入是 `buf` 开始的完整 `length`，不是 texturedataindex/retained prefix，也不是预处理后的模型。
3. `firstCRCDone` 已置位则必须与 `initialCRC` 比较，不等仍 `Sys_Error`；未置位则记录第一份 CRC 并置位，保留当前后续分支。

宿主旧 ProcessBuffer 的 `for (i=0; i<nBuffer; ++i)` 对零及负长度均不读取输入、不改变状态；等价优化必须保持宿主这个已有边界。原版回放只证明非负输入算法，不能反过来改变宿主负长度行为。

父任务需另验证新旧宿主 CRC 对完整模型与任意分段一致，资源实际变更仍能触发拒绝，再测相同地图切换时 CRC 阶段与总耗时。本轮不改变这些规则，不删除校验，不以“原版似乎没调用”作为绕过宿主验证的理由。
