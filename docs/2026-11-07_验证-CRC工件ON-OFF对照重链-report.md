# CRC ON/OFF 已有工件对照重链

2026-11-07，迁移后续窄任务。当前阶段只完成工具修复、输入核对与计划，没有编译、链接或部署。父协调保留运行与链接窗口；本报告不将已有对象重链称为迁移后源码完整构建。

## 输入与边界

`tools/Build-CSOLCrcBaseline.py` 新增 `prepare-relink`、`relink` 两模式。它们使用迁移前已核的 `build/csol-client-crc/client` 原213对象（109直链、104位于VGUI静态库），以及 `build/csol-client-crc-baseline/model_crc.cpp.obj` 的既有OFF对象，SHA-256为 `7db63727a3d0855ba990a49a578ae0696cf36f9945c67e7f113129b9703a6ffd`。

OFF对象复用之前真实成功的单TU编译命令及产物身份；其历史源码/实际include已在迁移前核同。新流程核对这份编译命令相对ON仅修改CRC定义与输出路径、增加include诊断选项，复核原213对象和静态库成员集合，以及OFF对象对应的历史回执。当前产品源码不读取、不复制、不重新编译；旧报告中的原路径是工件来源记录，不作为迁移后的编译入口。

实际链接参数来自旧计划保存的Ninja展开命令，保留CMake `vs_link_dll`、manifest处理、x86、ThinLTO和单线程设置。两版命令只有指定CRC对象及各自输出目录不同；其他对象与库相同。输入是对象、静态库、系统/CRT库、工具、环境及来源回执。旧命令没有外部manifest文件；默认嵌入manifest由已核同的CMake/链接工具执行。

## 修复旧验证缺口

旧OFF编译和重链退出0，但旧runner因CMake吞掉成功链接输出而拒绝验收，且遗漏构建前的 `libcpmt.lib` 摘要；旧失败和所有旧产物完整保留。

实际CMake版本为4.3.4。已核[该版本的 cmcmd.cxx](https://github.com/Kitware/CMake/blob/v4.3.4/Source/cmcmd.cxx)，正文SHA-256为 `7e290325ad271c122aafff87e02736d7c2d0604d0b2d8bbdc7df8df89ba54416`：`VisualStudioLink`读取环境变量 `VERBOSE`，`RunCommand`在该变量存在时打印成功命令输出；此包装器不支持 `--verbose` 参数。

准备阶段使用真实CMake包装 `cmake -E echo` 做输出捕获探针：不设置VERBOSE时标记被吞掉，设置后标记可见；没有调用编译器/链接器，也没有生成DLL。正式重链将同时设置 `VERBOSE=1` 和链接器 `/verbose`，从实际 `Reading ...lib` 日志核对读取的每个库。任何缺失日志、未登记库或PDB贡献库未出现在读取日志中都拒绝通过。

新计划预先记录22个库，包括明确链接项、静态库、此前PDB观察到的贡献库及MT默认CRT库；补入 `libcpmt.lib`。所有登记输入在准备、每版链接前后都校验SHA；新增产物采用全新目录，已存在的目标目录直接拒绝，旧对象和DLL/PDB不覆写。

## 准备回执

实际命令：

```powershell
python tools/Build-CSOLCrcBaseline.py prepare-relink --output build/csol-client-crc-relink-20261107
```

计划位于 `build/csol-client-crc-relink-20261107/plan.json`，登记256输入、22库、213原对象及独立OFF对象。记录范围明确为 `artifact-only-relink`、`currentSourceBuild=false`、`compilerWillRun=false`。

`preflight-checks.json`保存准备检查：有效计划通过且256身份保持；OFF误用ON对象、额外链接对象、单边加入未授权链接设置、复用已有输出目录四种错误均拒绝。真实包装器的有/无VERBOSE输出探针也保存于回执。未以这些检查代替真实重链结果。

首次准备在输出目录创建前拒绝了把 `/implib:CSOLSupport.lib` 误识别为输入库的情况；修正输入枚举以排除输出开关后，准备成功。没有因此生成或覆写任何旧产物。

## 下一步

等待父协调授予单job链接窗口，再执行：

```powershell
python tools/Build-CSOLCrcBaseline.py relink --output build/csol-client-crc-relink-20261107
```

命令将依次生成新 `on/CSOLSupport.dll/.pdb` 与 `off/CSOLSupport.dll/.pdb`，核实际库读取、DLL/PDB GUID-age、CRC标识与资源刷新关闭边界，再复核输入。未进行此步骤前，不宣称新对照产物或实际库读取验收完成；即使完成，也仍不等于性能、游戏行为或迁移后整树重编通过。

## 首次执行与独立续跑准备

父协调随后授予单job窗口。ON实际链接43.8136403秒，退出0；CMake成功透出完整1,229,510字节日志。验证器随后把 `Reading windowscodecs.lib(guids.obj)` 的归档成员行误作独立相对库路径而失败，按首败停规则未启动OFF。原 `result.json`、`link-on.log`、ON产物保留不改，没有重链ON。

父协调明确授权修复解析并准备续跑。解析现在区分绝对库打开与归档成员引用：成员必须唯一对应真实绝对打开行；未知真库、缺失/歧义成员对应以及无实际日志均拒绝。不是忽略全部相对行或吞掉未知库。新检查包含绝对库＋成员成功例，以及未登记真实库、孤立成员、未解析相对库打开、缺失日志四个拒绝例。

新增 `prepare-resume` / `resume-relink`，准备命令：

```powershell
python tools/Build-CSOLCrcBaseline.py prepare-resume --output build/csol-client-crc-relink-20261107-continue
```

该模式只读取已有ON工件和真实日志，核对22个实际打开库均与构建前SHA相同、PDB贡献库均可追到打开行、x86及DLL/PDB一致。ON补验通过：DLL SHA `1e9058441a5c024ce0bad588f2c6133a2afa577e4b058b46b20f46f8b7cfb13e`；PDB SHA `c98c9039e752a1e3aa325d3c7aa0cd26a3a4fc2b671427a643081876b0e221ca`。完整身份与读取清单位于续跑目录 `plan.json` 的 `onVerification`，解析检查在 `parser-checks.json`。

原256输入中255项保持；工具自身已改变，明确记录旧SHA `11f8ab056937e7121496e1c80158ed46b2e1aca3d3ba6605511f8f1c394e9e10`、新SHA `e8b618b298b6687eecc7edae2ad4bca73a527c19515fec5b983bf095067d41b6`。续跑计划登记新工具、原计划/失败回执/日志和现ON产物SHA，不冒称原256项全部未改。

下一步仍需父协调给OFF单job窗口，届时只执行下列命令，OFF输出位于新续跑目录，原ON不会重链或覆盖：

```powershell
python tools/Build-CSOLCrcBaseline.py resume-relink --output build/csol-client-crc-relink-20261107-continue
```

截至本追加，OFF未启动；只有ON重链及补验完成，没有编译、部署或实机收益结论。

## OFF续跑完成

父协调随后授予OFF单job窗口，实际执行上述 `resume-relink` 命令，完整链接43.178410秒、退出0，后验全部通过；没有重链ON、没有执行编译器。最终回执位于 `build/csol-client-crc-relink-20261107-continue/result.json`，包含实际完整argv、耗时、日志SHA及两版的产物/库身份。

OFF产物为续跑目录 `off/CSOLSupport.dll`，SHA-256 `721cbb691d510ae70eb4e5ac1a62e1e9c9df93a3ff33bf8b8a28b726d21f8c96`；匹配PDB SHA-256 `efbb73a01eed509ee5ed6dbca99d898c51a44901df6999988cf98ddefb2d8457`。ON继续使用前一目录中已核的 `1e905844...` / `c98c9039...` 产物，结束后SHA保持。

两版各22个实际打开的库均核对构建前SHA且两版集合相同，PDB贡献库均能追到实际库打开日志；x86、各自DLL/PDB GUID-age匹配（ON `7aa6d95d-5590-9dbf-4c4c-44205044422e`，OFF `3f137089-3c9d-ed14-4c4c-44205044422e`，age均1），ON有CRC hook标识、OFF没有，两版都没有刷新接口标识。原213对象、保留的255个旧输入及新续跑263输入全部核同；工具变更仍单列旧/新SHA，不回写旧失败为成功。工具/日志与现ON产物也在续跑输入中保护。

本阶段完成迁移前既有工件的ON/OFF完整重链及续跑身份核验，不代表迁移后当前源码全编、运行稳定性或性能收益。子代理未部署、未启动游戏；链接窗口已归还，旧失败和所有前序产物保留。父任务后续部署与实测单列于[同源换图对照](2026-11-07_验证-客户端CRC同源换图对照-report.md)。

## 审计发现与后续工具修复

独立审查确认一个历史证明边界：首次ON成功链接后，库日志解析失败发生在产物SHA记录前。ON DLL/PDB的首次SHA到prepare_resume才保存；因此只能直接证明续跑准备后未变，不能从原失败回执独立证明链接结束至续跑准备之间未发生替换。本轮没有发现替换证据，时间戳与链接完成时间一致，但时间戳不替代当时SHA。

父任务在实际两版重链结束后修正工具：每次成功链接立即保存独立linked-products身份回执，再解释库日志/PDB。续跑若存在该回执，必须核对三件产物路径、SHA及与失败结果的一致性；旧历史没有锚时明确记录first-captured-after-link-failure。此更改不回写旧计划或失败，不为日志解析重链已成功产物。helper真实临时文件14项检查通过，覆盖三产物SHA、拒绝覆盖和任一产物缺失；生产DLL未因此改变。

续跑锚另做12个真实临时文件场景，初轮10/12发现显式null被误当历史缺失；修正为存在但非法即拒绝，同12场景全部通过。覆盖文件锚/结果锚/双锚一致、冲突、错误或重复路径、SHA变化、文件缺失、检查中变化及历史无锚。仅替换昂贵的产物解释步骤，真实路径/哈希/计划校验执行；未重新链接。两轮回执分别保存在 `build/csol-crc-resume-anchor-review-20261107/result.json` 与 `result-02.json`，旧失败保持。
