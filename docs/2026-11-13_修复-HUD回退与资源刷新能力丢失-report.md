# HUD 回退与资源刷新能力丢失

踢击结束重播攻击已由用户实机确认修复；随后用户报告 HUD 回退。当前发现是客户端构建/部署能力不匹配，没有修改原版 HUD 布局、字体、颜色或校验规则。

## 故障证据

B 故障客户端 CSOLSupport SHA-256 为 b048e9a939596024bde17a7ff6088da6844758a42b4551d1d4a5c97f57862f13，缺少 GoldCraftCsolResource002 注册项；其构建缓存 CSOL_RESOURCE_REFRESH=OFF。同一安装 GoldCraft.dll 的 SHA-256 为 bb2f0a1cdeb330086268d9f31de277cf7cf139307ef265845ba86568e1cfb7ed，仍消费该接口。服务端 swds.dll 为 fd8c3b9625bdafabac6e97519a22dc17c50fa4155c1bf0f711f705acea8cf36d，保留 v4 SHA 清单实现。

此前实际 PID 44272 的匹配 PDB 只读采样：surface、schemes、extension 均存在，csol_hud_style=1，additive=1，但 loaded=0、moneyLoaded=0。具体地址只属于该进程映射；不能在重启后沿用。记录 build/csol-kick-phase-live-client-20261113-01/hud-gates.json。

build/csol-hud-root-regression-20261113-01/before.json 保存17项图像的尺寸、完整SHA、文件ID、链接数和时间。B游戏目录中12项内容错误、5项不存在，下载目录17项均与当前服务器逐SHA相同且尺寸符合编译契约。例如人物背景应288×85，游戏目录实际24×24；金钱背景应191×28，实际24×24。

这些错件来自此前明确保留的自动资源修复试验（build/resource-bridge-20261108/bad-seeds.json）；它们曾由内容寻址覆盖层正确覆盖。不能把下载目录文件存在当作实际VFS消费者正确，也不能用人工覆盖原始错件代替自动修复。

| Evidence | Finding | Path |
| --- | --- | --- |
| 错件尺寸、17项下载/服务器SHA及真实loaded=0 | 原有尺寸校验拒绝错误资源，随后正常保留原生HUD | native/csol/client/hud_style.cpp 的 Prepare、PrepareMoney |
| CSOL构建OFF、实际DLL缺注册项，GoldCraft仍要求该项 | 新客户端撤销了资源更新provider，无法协同挂载/刷新原本正确的缓存 | native/client/resource_bridge.cpp 的 Providers；native/csol/client/resource_refresh.cpp |
| 当前Build-CSOLSupport参数缺省会无条件写OFF | 重用已经开启的缓存时也可能静默撤销能力 | tools/Build-CSOLSupport.ps1 |

## 修复范围

在现有源树和可复用编译目录重建 CSOL_RESOURCE_REFRESH=ON 的DLL。保留踢击相位修复及当前领域改动。原始错误资源保持，使用既有入服SHA/HTTP/内容寻址覆盖与owner事务恢复HUD，不删校验、不强制ready。

新增 tools/csol_resource_capabilities.py：读取真实x86 PE32，检查已装GoldCraft消费者与待部署CSOL的确切接口注册标记；缺provider立即拒绝。这只是静态预检，不能证明CreateInterface实际可用或HUD渲染正确。

Build-CSOLClientCheckpoint新增 --deployment-consumer，在编译前核CMake开关，编译后核真实DLL注册标记，并封存消费者/检查工具身份。Deploy-CSOLSupport在ValidateOnly及真实写入前执行相同检查。Build-CSOLSupport仅在调用者显式提供ResourceRefresh时改该开关；省略参数保留已有缓存，新的普通构建仍遵循CMake默认值。

检查工具只在构建/部署冷路径读少量DLL，无游戏帧内开销。本次没有把时间测量或受控断言当成实机性能结论。

## 验证与下一步

已保存故障文件和真实状态；完整ON构建正在进行。下一步用实际旧DLL拒绝/新版接受对照，执行资源owner事务与踢击预测回归，然后只更新B的DLL/PDB及登记。专服现有v4引擎保留。B入服后须确认真实v4、最终SHA覆盖、两组HUD就绪与画面；编译、部署、入服及画面验收分别追加，不提前声称完成。

## 2026-11-13 最终后台检查点

完整构建最终 build/csol-hud-refresh-full-20261113-03/client/receipt.json：成功，实际548个编译依赖及广域输入均稳定，DLL SHA-256 a9e316bd5e0a3f383b212fe3cdf326d11c75f8389eca447d3c5e2c0c5e6c8bc3。前两轮编链本身0错误，但一轮新开启接口头尚未纳入冷依赖且CMake共享输入变化、另一轮Ninja重生成使构建控制变化，均保留拒绝回执；第三轮才是可部署候选。

最终实际DLL能力对照 build/csol-hud-refresh-capabilities-20261113-02/receipt.json 四组通过：旧DLL缺provider拒绝、OFF缓存拒绝、ON缓存及最终DLL接受。检查对象与工具均前后SHA稳定。

build/csol-hud-refresh-owners-20261113-02/receipt.json：实际HUD owner6项、资源事务108项通过，旧owner不清目录负对照按预期失败；未将受控provider当成实机。首轮旧夹具假设titleFont绑定一直保持77，与已发布固定字体缓存契约不符，保留失败；新夹具通过真实MenuFontCache证明绑定清零后仍复用同一HFont，不再分配/配置。补齐踢击和地图计数owner，使25个失效入口都受检。夹具改动保留在原有尚未发布资源刷新域，不混进本次工具窄提交。

build/csol-hud-refresh-kick-20261113-01 中host31项及实际K3 predictor21项通过，前后输入稳定；本次构建保留踢击相位恢复。用户此前“踢击测试正确”单独记为真人确认，不冒称自动采样证明。

B已停止后原子更新3文件：DLL、匹配GUID/age的PDB及客户端部署登记，见 build/csol-hud-refresh-deploy-20261113-01/{plan,applied}.json。GoldCraft/Renderer、服务端MP/引擎全部逐SHA保持；没有重启专服或启动第二客户端。目标旧件独立备份、ReplaceFile保留目标ACL，新目标nlink=1。没有修改受保护安装。

build/csol-hud-root-regression-20261113-01/after.json确认17项HUD在B游戏根、下载目录与服务端都逐SHA未改；12个错误游戏根文件的正确内容寻址对象已在缓存，其余5项来自正确下载目录。既有错误原件保留，用于下一轮真实自动修复/VFS消费者复验。

调用界面工具list_windows时收到实体Escape停止，之后未再调用任何Computer Use或输入。已完成后台构建和离线部署，尚未启动B复验真实v4能力、最终HUD ready/moneyReady和画面，不将本次标为HUD实机完成。下次明确恢复界面操作后，从这份已部署新版继续；不回滚踢击、不人工覆盖故障资源。
