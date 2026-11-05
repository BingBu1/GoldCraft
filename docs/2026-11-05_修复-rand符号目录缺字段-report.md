# rand 符号目录缺字段导致客户端启动退出

修复宿主符号目录生成器。客户端启动时的 `unavailable matched client import: rand` 来自已存在的目录记录被实际解析器拒绝，不是 `rand` 导入不存在，也不是需要替换随机数实现。

## 证据与边界

- 原宿主 `client.dll`：x86，基址 `0x10000000`，SHA-256 `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`，CRC64/XZ `124D6034A6B28B40`。使用工作区独立副本，未修改受保护安装。
- 原 PE 和本次 IDA 均确认 VA `0x1003743A` / RVA `0x3743A` 为六字节 `FF 15 20 F2 0B 10`，即 `call dword ptr [0x100BF220]`。原 PE 导入表确认 IAT RVA `0xBF220`、名称 RVA `0xE9AD8`，提供者 `api-ms-win-crt-utility-l1-1-0.dll`，名称 `rand`。IDA 加载器将 IAT 内容清零，导入表身份以原始 PE 核验为准。
- 实际宿主源码 `external/MetaHookSv-20261007/MetaHook/src/GameData.cpp` 的 `NormalizeGlobal` 要求七个字符串字段。旧生成器只写 `gv_rva` / `gv_va`，其余五项缺失，使该条目报 `global payload is missing a required gv_* field` 并被跳过。
- `native/csol/client/shared_random.cpp` 查询 `CSOL_ClientCRTRand` 后读取宿主 IAT；初始化查不到已被拒绝的记录，主动报错退出。该文件、随机源和随机算法本次均未修改。

这是宿主目录合同适配，不改变 CSOL 武器行为。调查和测试文件保存在 `build/skull3-audit-20261105/startup-rand-01`；原模块、IDA 数据库及工具输出不进入源码发布。

## 源码修改

`tools/Build-ClientGameData.py` 保留原模块身份、导入 DLL、名称和槽位验证，再校验原六字节 CALL，发布完整字段：

| 字段 | 值 | 原依据 |
| --- | --- | --- |
| `gv_rva` | `0xbf220` | 原 IAT 槽位 |
| `gv_va` | `0x100bf220` | 原映像地址 |
| `gv_sig` | `FF 15 ?? ?? ?? ??` | 原间接 CALL 编码 |
| `gv_sig_va` | `0x1003743a` | 原引用指令 |
| `gv_inst_offset` | `0x0` | 签名起点即指令 |
| `gv_inst_disp` | `0x2` | 绝对地址操作数起点 |
| `gv_inst_length` | `0x6` | IDA 指令长度 |

其余 35 条 supplemental 记录、目录身份以及公共插件符号去重规则保持不变。

## 验证

- `tests/csol-shared-random/test_catalog.py`：真实生成器七组正负案例通过，包含匹配模块以及错误导入 DLL、名称、槽位、名称 RVA、元数据身份。正确输出须包含完整七字段，并独立对照原 PE 指令和操作数。
- 新增 `tests/csol-shared-random/catalog_loader.cpp` 和 `run_catalog_loader.py`：直接编入实际 pinned `GameData.cpp`，用 x86 Clang / C++20 构建并执行。旧目录加载后 `rand` 被拒绝，新目录通过查询，地址与指令元数据正确；逐一删除七字段均被真实 `NormalizeGlobal` 拒绝；其他 shared-float 符号保留，错误 CRC 查询被拒。编译、链接及两个独立进程均退出 0。
- 旧七组生成器测试仅断言两个地址字段，没有执行目录消费者，因此未能发现原错误。本次补上实际消费者验证，不用模拟的 Python schema 替代。
- 新独立候选 snapshot：9738 字节、36 条；SHA-256 `3ec2b603aa7495650b967e7cc773f84a7c8b0f12c64cf15b94edda18969d88e3`。配套 index SHA-256 `827bc3a7ce4f6247c7419fa9a26b8ceb22db74d137dbf757339c1df493ce344d`。

本子任务未构建游戏 DLL、未修改运行目录、未操作 Git。父任务已收到两目录候选并负责部署、启动及游戏验收；静态和解析器验证不代表武器玩法或多人验收完成。

## 共享文档待父任务应用

更新当前 rand 目录故障项：原 PE / IDA 已核，生成器已补七字段，七组生成器案例与实际宿主解析器旧拒新收通过；继续记录父任务单 B 启动、入服和武器实测结果。TODO 仍保留完整实机要求，不单凭目录修复关闭 SKULL 验收。

父任务部署与实机：仅B的snapshot/index两文件独立原子替换，回执位于 `build/skull3-audit-20261105/rand-catalog-deploy-01`。原client与CSOLSupport DLL未变；B再次启动后成功入服，初始化错误消除。用户随后明确实测正常。自动截图两入口超时，不宣称独立视觉或多人验收。
