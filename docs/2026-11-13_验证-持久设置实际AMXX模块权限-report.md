# 持久设置实际AMXX模块权限验证

实际 `goldcraft_amxx.dll` 的设置 native 已通过独立加载回归：**109 检查、0 失败，5009 个记录输入稳定**。本次只构建 AMXX 模块及测试 provider/EXE，没有构建完整 MP、部署或运行游戏；已冻结 Pawn/parser 未修改。

## 证据

E1：[native.cpp](../tests/csol-custom-admin/native.cpp) 使用 `LoadLibrary` 加载实际模块，调用导出的 `AMXX_Attach`，捕获其 `AddNatives` 注册的真实函数地址，再直接调用 `csol_custom_api_version/info/read/edit`。没有把生产 native 实现复制到测试。控制边界为匹配 SDK 的 `IsPlayerInGame`、`GetPlayerFlags`、`GetAmxAddr`、`SetAmxString`、`RealToCell` 和 `CellToReal`。

E2：[provider.cpp](../tests/csol-custom-admin/provider.cpp) 是隔离 `mp.dll`，只暴露准确版本的 `CSOLGameplaySettings001`。显式提供 userid、状态、字段、编辑拒绝以及查询时的可重入回调；不执行真实磁盘保存或伪装完整游戏规则。

E3：[run.py](../tests/csol-custom-admin/run.py) 从既有 `compile_commands.json` 读取真实模块编译命令，要求 x86/C++20/O3/ThinLTO/AVX2/precise。只运行 common CMake 的 `goldcraft_amxx` 目标，实际三 TU 编译/链接成功，然后独立复制构建产物至测试目录加载，构建新 provider 和测试 EXE。无源码快照、运行服或 Git index 操作。

E4：最终本地 `build/custom-admin-native-20261113-04/receipt.json`、`native-tests.log`：success/inputsUnchanged 均 true，109/0；359 个观察到的夹具编译头文件都在预记输入中。实际模块 137728 字节，SHA256 `be5f6ad6f0fea06fb0861a168679674cb201d06def33ca4598d78e0139c385fb`；对应 `native/amxx/module.cpp` SHA256 `f52e351b27997905b809ff3c4ab0cd8b8754f8db94c753486be98d69c39a6d77`。

复现从项目根 Git Bash 运行：

```bash
python tests/csol-custom-admin/run.py --help
```

传入现有 common build、匹配 compiler environment 和全新 `build` 输出目录。测试 provider 仅用于此独立 EXE，不能安装到游戏服。

## 结论与分层

F1（E1/E2/E4）：普通玩家、断线管理员、actor 0/非法槽位、userid 0/负值/不匹配都不能取得永久编辑权限。死亡但仍 ingame 的管理员可以提交，不引入 alive-only 限制。引擎 userid 查询回调撤销 `ADMIN_CFG` 或 ingame 后，模块真实第二次检查在进入 Edit 前拒绝。

F2（E1/E2/E4）：查询回调使同槽 userid 从 71 变成 72 时，模块可能已取得旧 userid；最后 provider `Edit(actor, userid, ...)` 再核身份后拒绝。此结果依赖权威 provider 的最终身份检查，不能写成模块自己二次查询了 userid。实际服务器适配层仍必须保留该检查。

F3（E1/E2/E4）：精确参数字节数、空参数块、字符串长度 -1/4097 在进入 provider 前被拒绝；0 和 4096 的已声明边界可用。revision/count/busy/result、类型/数值范围、optional unavailable 和中文说明通过实际回调写入，截断边界保留哨兵。过时 revision/非法字段的权威拒绝状态由模块原样返回。

F4（E1/E2/E4）：该版本 `amx_ftoc/ctof` 经 SDK 请求的转换回调，测试按 `REAL` 的实际 32 位宽度实现。NaN/正负 Inf 经模块转给权威 provider，provider 明确拒绝；不把 provider 的非有限校验归为模块自身校验。有限值 1.125 到 double 精确保留。

F5（E1/E2/E4）：provider 尚未加载/接口未就绪时不缓存成功；真正 Detach 后清除 settings 缓存，重新 Attach 且接口不可用时，旧 provider 不能被继续使用。

P1，callflow：实际 DLL `AMXX_Attach`→真实注册 native→模块权限/参数门→受控 userid 回调→模块权限重核→权威 provider 最终身份与数值检查→结果返回。各阶段分别计数与断言，不以返回正数代替实际调用证据。

## 保留的失败与验收边界

`-01` 在 31 检查后触发未覆盖 SDK 回调；`-02` 独立命名诊断确认是 `RealToCell`，均保留失败。按匹配 typedef 补转换边界后，`-03` 的大量临时诊断 thunk 增大 ThinLTO 成本，外层等待中断，其收据保持 incomplete/false，未算通过。随后撤去诊断表，仅保留单个拒绝 fallback；核对没有遗留本任务链接进程且未终止其他任务，最终 `-04` 全部通过。新驱动在每条命令启动前记录 pending，超时保留命令与部分日志。

本次验证是真实模块边界，未覆盖真实 AMXX 运行时管理后台、实际 SettingsStore 文件提交、提交期间权限变化、菜单视觉或完整游戏行为。Pawn VM 的 1059 项、纯配置历史 504 项、当前分组配置版本和磁盘持久测试须分列，不能合称整条持久设置已经实服通过。分层结果已登记本任务连续文档，下一步为配套全构建及实际权限/保存/菜单联测。

源码检查点：踢击 `8ad04a3`、M菜单与统一JSON实际适配器 `5fd2627e7d81d35a2dde24c65798f267b50e6841` 已提交并推送 main，远端SHA核同。两批精确索引分别3710/3721项扫描零问题；完整当前专服构建仍在进行，未据发布声明部署或实际玩法完成。
