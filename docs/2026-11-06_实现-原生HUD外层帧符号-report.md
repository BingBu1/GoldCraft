# 原生 HUD 外层帧符号

## 实施前证据合同

2026-11-06：父任务指定在已发布五符号阶段 `f649a811` 上增加 `GoldCraftNativeHud_HostFrame`。当前工作区三目标文件与前次最终交接SHA一致；父在独立发布目录管理Git，本工作区不操作index或publication。只改engine独立目录和两份独立测试；现有两个生成器无需修改。

原宿主 `hw.dll` 是 x86 PE32，ImageBase `0x10000000`、3,598,176字节，SHA-256 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`，CRC-64/XZ `6ef7192cd8254e2d`。原件在 `build/resource-refresh-client-20261105/ida/hw.dll`，没有使用受保护安装或其他hw版本。

Evidence → Finding → Path：

| 原证 | 结论 | 实际修改位置 |
|---|---|---|
| `host-unwind-20261106/ida-evidence.json`，SHA `9f02d9c66c44f11a11b1c98056ab10762336f7682b7626f59757ac087624bfe4`；完整外层函数 `101D28B0..101D2B32`、160条指令、643字节 | 新记录必须为engine/function，RVA `0x1d28b0`、size **`0x283`**。入口 `_setjmp3` 使用E40，非零跳共同尾；不可用内层RVA `0x1d43d0` 的E00边界 | `native/client/native_hud_engine_10210.json` |
| 同证据outer_frame及caller：caller `102211C5` 取栈输出地址，转换并压float、压int、压指针，`102211D4`调用、随后清12字节并使用EAX；外层读取arg4/arg8，写32位输出，原样传float给内层，返回32位状态 | 确切ABI为 **`int __cdecl(float, int, int*)`**。目录只给地址与extent，不表示已经安装此ABI的包装或证明运行时退栈 | 独立目录测试检查原入口/大小；父任务负责真实包装 |
| `host-unwind-20261106/verify.py`与已保存原PE字节 | 本轮实施前重新通过7窗口3258字节及两项CRT导入核验；公开目录仅增加外层入口32字节和caller引用26字节，不公开完整反汇编 | `tests/native/test_native_hud_catalog.py`增加该本地捕获格式读取；`tests/native/native_hud_catalog_tests.cpp`增加一个Expected项 |

新原证来自 [真实退栈边界报告](2026-11-06_取证-原生HUD真实退栈边界-report.md)。其原handoff登记报告SHA为 `bc3a43686a19184593bd5e774e242ce7cb8036c205cf87de2e0f27a6da1fc98c`；父已将报告链接转为代码路径，当前SHA为 `c024d114584b23636d172e8ef2322765b6bc00522faa491a17e66d392b97d298`。本轮按父已说明的文档格式差异接收，IDA正文、验证回执、verify.py与scope的原SHA均匹配。新的只读核验输出保存在 `build/native-hud-hostframe-symbol-20261106/original-verification.json`。

## 验证与验收边界

计划只重跑更新后的窄目录生成与真实MetaHook解析器组，保留五符号正例、错误身份/kind/module拒绝和原符号兼容检查；x86单job编链，不执行原DLL。目录通过不能代替外层frame/callback token接线、真实Host_Error返回、下一次自然VidInit或游戏画面验证。

外层trampoline真正返回后，只可登记本token内旧回调已退栈。ClearState、Disconnect、reset observer均可能在longjmp之前；任意后续Frame和内层Frame返回不可追认旧回调。父任务继续真实生命周期hooks；本轮不修改plugin/runtime或共享连续性文件、不部署、不操作UI/Git/地图。

## 完成结果

- 实施前合同冻结为 `build/native-hud-hostframe-symbol-20261106/implementation-contract.md`，SHA `0e617e92b6ae5c38cb18df187b0d7bf7ce9a9daff8a01a47669d7cbbfd1249df`。
- 原PE/IDA重新核验7窗口3258字节通过。窄目录组覆盖原10窗和新增2窗，共12窗182字节；新增的32字节外层入口和26字节caller引用均直接来自同一原件与保存IDA。
- `validation01/result.json` SHA `2bcd9982185a1445c829dba906ec255cd0b2ea3f7c7d26d97f9776c41c965d70`：实际x86单job编链通过，515实际include身份保持。真实MetaHook解析器/文件hash/ResolveGameSymbol **90检查、0失败**，包括新增外层地址/643字节extent、错误kind/另一模块/改动文件身份拒绝；两生成器的错误身份PE反例也均拒绝且未创建输出。
- `catalog-delta.json` 确认生成engine目录只新增 `GoldCraftNativeHud_HostFrame` 一项，146→147；移除此项后与前次目录完全相等。client目录47项及其文件SHA保持。没有修改两份生成器。
- 本轮源码范围只有独立engine JSON与两份独立测试，另新增本报告。五符号阶段、两个生成器和共享随机测试均不再改动。完整源码/原证SHA、冻结副本及目录差异回执见 `build/native-hud-hostframe-symbol-20261106/handoff-final.json`。

以上验证解析和静态ABI地址，不执行原Host_Frame，不证明真实Host_Error跳转或HUD生命周期恢复。父接线应使用 `int (__cdecl*)(float, int, int*)`，保留三个参数及EAX返回；仍不得把内层 `0x1d43d0` 或reset observer作为退栈证据。
