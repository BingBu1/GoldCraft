# CoF 场景波动撤销武器权限

本轮优先修复人类反馈的“所有 CoF 武器偶发准星消失，同时不能射击／换弹／切枪”。已复现并修复一条共用宿主生命周期错误；源码与真实 GL 回归已通过，完整构建、部署和游戏结果在下方分开记录。尚不能把这一条修复扩大为全部故障来源都已排除。

## 证据与根因

| 证据 | 已核事实 | 对应路径 |
| --- | --- | --- |
| E1：`build/cof-all-disabled-live-20261115-01`、`build/cof-all-disabled-typed-live-20261115-01` | 同一连接代际6／效果资源7，客户端能力位31短暂变0再恢复；另一次活动VP70的实际`bound_`变0，状态依次Cancelled、MissingBackend，准星关闭。外部读取不是原子快照，不将未同帧读取的值冒称完整因果证明。 | `guest_cof_admission.cpp::Publish` → FCVAR_USERINFO → `guest_cof_host.cpp::PreparePlayer` → 权威owner退休／武器重绑。 |
| E2：实际Renderer API及`gl_rmain.cpp::CMetaRendererOpaqueScene::QueryFrame` | QueryFrame只借出本次真正主视角opaque帧；辅助视角、临时可见性未就绪、颜色／深度不符等合法返回false，不表示已准备的地图后端已销毁。 | `guest_cof_scene.cpp::DrawGuestCofOpaqueScene` 原先每次先清`pointSceneReady`；任何早退都把下一次HUD_Frame中的全部五枪能力撤销。 |
| E3：原CoF client的完整IDA函数与原始PE | `1005C0F0`的32×124点光分配仅依据key和原时钟／寿命；实际消费者`1004BC30 → 1004C05A`调用分配器，没有依赖“上一绘制回调成功”。`1005A000`是地图／gamma设置变化判定，不能当逐帧武器许可。 | 分配器、数学和已还原特效不改；修复本项目新增的宿主后端生命周期。 |
| E4：同夹具旧败／新过 | 真实NVIDIA WGL Core4.4、生产scene/admission及点光renderer直接运行。旧版六断言失败；新版46检查零失败。一万次有效／拒绝帧交替不产生新的userinfo或冷准备。 | `tests/cryoffear-scene-admission`，回执见下方。 |

E3样本为工作区原CoF `client.dll`，PE32 x86，VA基址`10000000`，SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`；原MP `hl.dll`本轮重新核为 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。前者三个完整IDA区共1477字节／378指令，指令边界与实际PE逐项核同。最初未自动分析造成的缺失指令输出已经排除，不作为证据。

修改前证据及源SHA记录在私有 `analysis/cof-weapons-20261111/client-evidence/admission-lifetime-20261115/PREEDIT.json`，含`dirty.json`、`allocation.json`、`caller.json`。原文、样本和运行数据不发布。

F1（`host_adaptation / validated`）：暂时不能绘制的帧被错误提升成全枪失效，违反后端真实生命周期。修复保持已通过真实opaque帧验证的后端到`ClearPoints`退休；每次绘制继续独立核验世界、PVS、颜色、线程、上下文和刷子容量。失败只跳过该次绘制，后续有效帧正常恢复。不修改射击、装填、弹药、伤害、时间或原效果数量。

## 实现

- `native/csol/client/weapons/guest_cof_scene.cpp`：`pointSceneValidated`表示当前地图／资源后端已经通过真实帧；本次局部`frameReady`只控制本次绘制。连接、资源、实际场景退休仍清零；补核效果资源代际，禁止旧后端借新资源。
- `guest_cof_scene.hpp`：澄清稳定后端与本次绘制的契约。
- `tools/Observe-CoFReload.py`：只读观察全部五枪的绑定／状态／动作字段；客户端仅读取当前精确匹配PDB确实存在的准备字段，优化掉的字段明确记为不可读，不冒充false。保留旧`pointSceneReady`与新字段的兼容记录。
- 新实际场景回归直接包含生产编排单元，执行真实点光renderer；其他服务明确为受控边界，不模拟成完整游戏验收。

热路径没有增加分配、读盘、网络、定时器或资源扫描。直接消除了拒绝帧引起的userinfo更新和服务端owner反复重建；这属于已证调用消除，未做新游戏CPU/GPU采样，不声明FPS提升。

## 验证层级

- 旧版夹具命名冲突构建失败保留在`build/cof-scene-admission-old-20261115-01`，不是生产故障证据。
- 旧生产代码负例：`build/cof-scene-admission-old-20261115-02`，真实GL六断言失败。
- 修复后：`build/cof-scene-admission-fixed-20261115-01`，46检查零失败，实际依赖前后核同。覆盖缺帧、线性颜色、辅助世界、超刷子容量、一万次交替、错误线程／上下文、资源更换、连接更换和真正清理。
- 权威主服完整HUD闭包312资源通过，`hud-preflight.json`已保存。未覆盖故意损坏的客户端根目录资源。
- 最终完整客户端`1bbf7342169bc1ec37e1e4e9323c22768559cfa531a9527417b6cc8ece8a7d77`及匹配PDB已稳定构建；实际编译输入及广域源前后无漂移，资源刷新能力保留。首次完整构建期间补写头文件契约说明导致输入门拒绝，失败收据保留；最终02重新构建通过。
- 实际最终DLL武器注册2235检查零失败，`build/cof-scene-admission-final-registry-20261115-01`。
- 三文件已经原子更新本地单B并启动PID38140；`build/cof-scene-admission-update-20261115-01/deployment.json`、`loaded-identities.json`。当前专服PID36668及MP`60ba4f3f`保持，完整新DLL包含已在源码中的最新体力／HUD改动；不是回写先前旧服务版本。实际内存CodeView与DLL/PDB核同，GoldCraft`59498c0b`／ParallelDownload`00affa36`保持；312权威HUD和111菜单契约闭包通过，20个HUD／金钱数字纹理句柄均有效，金钱可见。玩家当时尚未持枪，weaponVisible=false按实记录，不冒称武器HUD画面已验。
- 用户选择自行测试，未再发出界面输入；双端只读采样进行中，实际普通射击／换弹／切枪的最终结论待本轮样本及人类反馈。受保护安装四核心SHA核同。

## 未完成与接续

完成普通射击／换弹／切枪的最终运行核对；本轮由用户自行测试，物理Escape停止仍有效，未通过其他输入接口绕过。Glock旧供体的袖口破面另项保留；981fb25a离线候选尚未部署或冒称手模完成。全部25个CoF库存身份及变体的完整Goal继续。

共享文件精确更新交接（不跨聊发送）：`PROJECT_STATE.md`记录共用撤权根因及本报告；`docs/PROGRESS.md`记录旧败／新过和最终构建／部署层级；`docs/GOAL.md`保持全范围；`TODO.md`只保留真实普通操作／手模／后续身份验收。协调窗口尚未分配，领域报告是本轮持久记录，不覆盖其他会话共享编辑。

## 后续退出故障与最终自测版本

本轮被动观察只有315次有效读：五枪能力始终31，但服务端slot1没有活动玩家对象。随后专服36668和B38140退出，5685次读取明确失败，不将其计为玩法通过。dump显示专服在换图创建挂载机枪时，`CMountgun::Precache → Mod_LoadModel → Sys_Error`因为`models/shell762.mdl`缺失而退出；客户端随后在`HudShutdown → ShutdownScenarioMapVote → DHANDLE::operator bool`访问已卸载VGUI接口。离线日志为`build/cof-shared-crash-20261115-01/server.log`、`server-details.log`、`client.log`，不是枪械射击崩溃的证据。

服务器shell762资源在14:25已被共享环境的后续更新补齐；只读核同原解包资源SHA `0386f3dd05a90f09fbe8f8447c0abd2b4adc6e802313af104dbc2daca62c1c7a`／8692字节。本轮未再次覆盖该资源；挂载机枪第二P变体仍缺，另列共享交接，不把这次补件当整张地图验收。

客户端退出修复是宿主适配：已读实际VGUI2Extension `CBaseUIProxy::Shutdown`，其`ClientVGUI_Shutdown`在原生卸载前，`BaseUI_Shutdown`在原生卸载后。实际SDK `PHandle::Get`需要调用`ivgui()`，而`Set(nullptr)`只清无效句柄。修改前依据记在私有`VGUI-PREEDIT.json`；不改变CSOL投票规则或布局。

`scenario_map_vote.cpp`新增ClientVGUI生命周期订阅：在原生VGUI仍有效时释放投票纹理／字体、退休子面板、清空借用接口；公开Shutdown保持幂等，并在回调迭代外注销订阅，避免修改当前被遍历的回调容器。晚到的HUD退出不再查询失效接口。该文件原有资源刷新／字体等其他会话改动保持，Git只暂存本轮窄补丁。

`build/cof-ui-shutdown-20261115-03`真实SDK接口／生产退出方法6检查通过、失效接口读取0、全部输入稳定；01／02夹具缺SDK包含路径的构建失败保留。最终完整client `2aac598552ed6ebb4eebcdfc207bc9caac32df0011d9898a5954e6b15a36694d`稳定构建，最终DLL注册2235通过；替代先前1bbf版本，三文件安装回执为`build/cof-ui-lifetime-update-20261115-01/deployment.json`。权威312HUD闭包、既有模型／下载组件和受保护安装核同。专服PID40176／B23000已恢复，用户自行测试；没有自动按键、没有最终退出或五枪玩法通过的结论。

共用撤权代码已发布main `9be9bfee8279e5570b65060f33b310b2affa5e2d`，GitHub逐SHA核同；4114文件／32314287字节精确索引扫描零问题。本节退出修复与最终收据另作窄提交，完整CoF Goal仍active。
