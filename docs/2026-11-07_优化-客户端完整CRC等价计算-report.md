# 客户端完整 CRC 的等价计算

2026-11-07，修改前的证据与实施边界。目标是减少模型重复加载时既有CRC计算的CPU耗时，保留全部输入字节、比较及拒绝规则；不把它当作模型缓存反复淘汰的完整修复。

## 原版与宿主证据

原版CSOL hw SHA `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`，原IDA ProcessBuffer VA `0x37867ef0`、表 `0x3809f170`，使用 reflected IEEE `0xedb88320`，支持任意未取反初值及分段续算。沿用已核9段/3694B与原指令回放，详见 docs/2026-11-07_取证-CSOL引擎CRC32与宿主模型校验边界-report.md。原版用单表串行展开；本次八表计算是宿主等价优化，不声称原版这样实现。

实际客户端宿主hw SHA `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`。ProcessBuffer RVA `0x1bc450`、Init `0x1bc440`、Final `0x1bc430`、ProcessByte `0x1bc620`、表 `0x2ba290`。精确PE/匹配PDB/运行字节经HIGHLOW重定位核同；宿主窄证据是PE反汇编与实际进程只读结果，不伪称新增原版IDA。证据保存在 `analysis/map-transition-active-20261107/client-cache/hw-model-crc-pe-evidence.json`，SHA `ce34649dba5618ce3e839c221fb09df7f17bdebf8c9492cb180f08348816c0bb`。

`Mod_LoadModel` 在cache命中时提前返回；cache miss且trackCRC/shouldCRC均开启时，RVA `0x2403e4/0x2403f7/0x240402`依次调用Init/Buffer/Final，之后才在 `0x240418` 判断firstCRCDone，既有CRC在 `0x2404d3` 比较。不匹配仍须拒绝，不跳检查、不延长连接超时、不以第一次检查成功强制后续成功。

`tests/client-crc32/replay_host.py` 已直接回放宿主原指令：1112组长度/对齐/任意初值、161组分段、196189次输入读取边界通过；256项IEEE表核同。只在独立模拟内存执行CRC，不加载DLL或初始化游戏。报告 `build/client-crc32/replay.json`。这不是编译后的新函数测试或实机提速。

## 精确实现边界

计划复用 `native/engine/crc32_ieee.h` 的八表Update，独立CSOLSupport按匹配宿主符号Hook原ProcessBuffer。cdecl签名为输出DWORD指针、字节指针、32位长度；输出保持未取反，Init/Final/ProcessByte和模型加载分支保持。

重要区别：宿主机器码将长度用于无符号比较，**不能照搬ReHLDS对负数长度不读输入的规则**。负长度不在本轮支持域，必须转回原函数处理，不在新函数静默接受。零长度保留读写CRC状态而不读取buffer；CRC输出与输入别名时仍在全部读取完成后写回。

新hook须纳入现有Hook管理与RemoveHooks生命周期，符号目录必须绑定宿主完整SHA/CRC64并校验入口字节。默认只改计算算法；禁止同时修改缓存预算或CRC启用条件。编译测试应覆盖原宿主机器码、新实际callback、分段/未对齐/保护页/别名及负长度转发。正式构建、运行模块身份、单B跨图和实际提速分别验收。

当前仍需审核根源码相对运行e6a18fd3的其它改动；不能把包含资源刷新WIP的整包宣称为CRC-only。源码直接修改项目，build仅用于对象、生成测试输入及记录。

## 本轮实现及离线验证

新增 `native/csol/client/model_crc.cpp/.hpp`，由已有 `InstallLoaders` 安装并归现有Hook管理；卸载沿原RemoveHooks逆序撤销。CMake `CSOL_OPTIMIZE_HOST_CRC` 默认为ON；OFF是受控基线，不解析新符号、不安装hook，继续执行宿主原函数。负长度转回原函数；非负长度只替换IEEE计算。`sequence_engine_10210.json` 新增绑定完整宿主身份的函数记录和入口字节，生成目录148符号/126指令范围/30读取调用校验通过。

`tests/client-crc32/run.py` 用x86/C++20/O3/ThinLTO/AVX2/precise编译实际生产helper。测试进程仅映射本地宿主CRC独立指令/表、按原PE HIGHLOW修正绝对地址，不调用DLL入口。原字节生成物只在build，不进入源码发布。

- **最终75615项断言通过**：原机器码与新编译helper，0–65536长度、多种对齐/初值、1025处分段、两端保护页、输出/输入别名、负长度原样转发。审查补上两次VirtualProtect成功断言后重新编译执行，最终回执 `build/client-crc32/native-guard-checked/result.json`；初轮记录保留。
- 150个实际角色模型，原机器码/新helper/独立zlib结果一致；各模型首、中、末字节变化均被检出；输入模型和源码前后SHA保持。
- 同批模型纯计算：原机器码 **2.173659秒**，新helper **0.552097秒**。交替先后运行；这只是CPU计算样本，不是完整入图提速。
- 实际MetaHook SDK编译生产adapter的ON/OFF两组均通过：启用组核符号名/类型、hook参数、真实callback、负长度转发和缺失符号抛错；关闭组未触及宿主。回执 `build/client-crc32/adapter/result.json`。

完整CSOLSupport已直接从项目构建成功，单job耗时579.403秒，当前候选为CRC ON、资源事务OFF、无嵌入字体。默认/显式关闭资源事务的两个真实SDK/GDI测试通过，工厂不广告刷新接口，字体成功/失败保持原GDI语义且无新增事务内存分配。完整输入、对象及PE/PDB核验继续；尚未部署，不代表画面或连接实测通过。

下一步按相同当前源码基线进行原CRC/优化CRC整体验证。根候选还包含已审计的SEQ/消息消费者差异，不能以本算法回归替代它们的验收。真实MetaHook入口跳转、original trampoline和卸载链仍须运行核验，测试替身Hook通过不代表实际挂接已成功。

## 完整构建身份核验

最终 `build/csol-client-crc/verification.json` 通过：6677项预登记输入前后不变，650项实际工作区依赖全部预登记，213个实际链接对象与命令/链接边同集；491项系统依赖仅在构建后登记，不能声称同样拥有前后摘要。x86、O3、ThinLTO、AVX2、precise及PE/PDB GUID-age已核。

- DLL SHA-256：`8acebb080f770ee52d0746e1e745d23fa689ac8cca953a93f2207e1aef056e0f`。
- PDB SHA-256：`4483944c0a8508905a5cae6993d59489791e12ce61293e1db2cb7a58d1f369c6`。
- CRC启用、资源事务关闭，实际DLL无新刷新接口字符串；核心CRC及字体源码与PDB摘要匹配。

尚未部署，实际Hook/trampoline、同源ON/OFF整图对照、重复缓存释放原因仍待。整候选包含SEQ/墙通知等根源码差异；本次CRC源码提交不代表全部根WIP已发布或玩法验收。
