# 换图重连超时修复

2026-10-31，用户报告 `Failed to reconnect after level change`。现场专服仍在运行，唯一 B 客户端后来已重新加入 `zs_deadend`；这条错误本身不是服务器崩溃证据。

## 原因与修正范围

ReHLDS 的 `SV_InactivateClients` 在换图时为原玩家槽设置重连期限。原有带外 `SV_ConnectClient` 会清除该期限，但正常客户端也可以沿用原 netchan，通过 `SV_New_f` 重新开始 sign-on。旧实现没有在这个成功入口清除期限；因此已开始正常加载、尚未 fully_connected 的玩家仍可能在 30 秒后被误踢。

修正在 `SV_New_f` 的原有早退检查和游戏 DLL `ClientConnect` 许可之后、发送 sign-on 数据之前清除当前槽的期限。不改变认证、普通 `sv_timeout`、未响应 `new` 的超时处理或拒绝连接的路径；没有通过禁用超时掩盖问题。

这是实际源码中确认的缺口。尚未保存此次用户掉线的完整网络握手，因此不能仅凭源码断言该次掉线的全部触发条件。

## 验证与交付

- 源码：`external/ReHLDS/rehlds/engine/sv_main.cpp` 已修改。完整原文件、最小三处差异和新旧真实函数提取保存在 `build/csol-reconnect-20261031/`。
- 自动测试：`tests/engine-reconnect/main.cpp` 使用真实 `SV_New_f` 与 `SV_CheckTimeouts` 函数体，控制网络和游戏 DLL 边界。覆盖第 1/32 玩家槽、正常加载超过 30 秒、真正失联、持续包但不发 `new`、重复/控制台/拒绝路径及时间边界。两组编译成功；新代码 18 项全过，旧代码恰在两个玩家槽的清期限和加载超时四项失败。各 273 条实际 include 记录均已登记，输入前后保持。证据：`build/csol-reconnect-20261031/validation-01/validated.json`。这不是完整引擎或实机测试。
- 引擎：仅编译 `sv_main.cpp` 并重新链接现用引擎的其余对象文件，两个步骤均成功；580 条实际 include 记录无未登记输入。新 x86 引擎 SHA-256 为 `9a85a362a78a103e3658f99445eaa1f548c864f9a03f0b713ee59869171e343a`。
- 部署：已正常停止旧专服，独立备份并原子替换 `swds.dll` 和 `swds.pdb`，隐藏启动本地 27045 端口、32 槽、`zs_deadend` 灾变专服。原客户端、游戏 DLL、启动器和文件系统模块身份保持，B 仍为唯一图形客户端。证据：`build/csol-reconnect-20261031/deployment/receipt.json` 与 `live/deployed-identity.json`。
- 实机：同一个 B 客户端连续两次执行 `changelevel zs_deadend`，自动重新入场，分别观察 59.457 秒、58.554 秒后仍为同一用户槽和网络地址，没有手动重新连接掩盖失败；前后 `sv_reconnect_timeout` 均为 30。现场停在原选人页，未改角色、资金或其他 cvar。证据：`build/csol-reconnect-20261031/live/live-result.json`。
- 验证边界：这是同图快速重载并跨越 30 秒后的在服验证；没有故意制造超过 30 秒仍未完成的实机下载，长加载边界由上述新旧真实函数测试覆盖。本次未验收其他地图或多真人同时换图。
- 原安装保护：只读核对 19,608 个文件元数据和 7 个核心文件 SHA-256，前后保持。测试前后没有写入受保护的原安装。

共享文档与源码发布由主协调统一接收；本报告不表示大灾变总目标或其他在途技能已完成。
