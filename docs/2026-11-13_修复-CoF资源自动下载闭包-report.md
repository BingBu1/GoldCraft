# CoF P345 配套部署与自动资源下载闭包

本轮已将当前 P345 配套客户端/服务端和57项资源部署到隔离测试环境，保留最新首图编号初始化与资源刷新能力。专服零玩家窗口恢复 zs_decoy/编号113/灾变2/32槽与允许难度1..6。随后实际HTTP逐项检查准确发现5项404，因此没有将该次部署计为整枪交付；已修源码与组件验证，完整引擎/GoldCraft/MP下载修复正在构建，最终HTTP和客户端验证待补。

## Evidence → Finding → Path

这是本项目宿主下载契约修复，不改变原 CoF 射击、装填、声音或状态时序。原行为继续绑定 MP0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63 和 client d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00 的既有证据。修改前映射为私有 analysis/cof-weapons-20261111/revolver-integration-20261113/host-download-contract-before.json。

| Evidence | Finding | Path |
|---|---|---|
| 实际HTTP57文件/52通过/5失败，http-verification02.json；引擎GC_HttpResources/Store/路径门 | inventoryitems 与 aurora 已实际预缓存，但服务器 HTTP 与客户端覆盖路径门都不认识它们 | goldcraft/cof_resource_paths.hpp；http_download_server.cpp；resource_overlay.cpp |
| 实际 client guest_cof_hud 与 authored presentation 输出 | sprites/weapon_cof_p345.txt、cp345.spr、p345_ammo.spr 已生成却未进真实引擎广告；已有本地资源掩盖缺口 | presentation_resources.hpp；guest_cof_resources.cpp；guest_cof_host.cpp |
| 实际GoldSrc/ReHLDS通用资源缓存、VFS与代际回调 | 必须在真实冷阶段全部预检、完成真实PRECACHE_GENERIC后才准入；0为合法缓存返回，负数与退役失败仍拒绝 | PreparePistolPresentationResources |
| 新07e0247构建能力保护；当前GoldCraft资源接口标记 | 不能用资源刷新OFF客户端覆盖本地已修HUD版本 | Build-CSOLClientCheckpoint --deployment-consumer；实际配套能力/PDB/菜单校验 |

路径修复仅允许 inventoryitems/guest/cof/weapons/*.txt 与 aurora/guest/cof/*.aur，经现有遍历、设备名、控制字符和扩展名检查之后使用；其他库存、插件、配置和相似前缀继续拒绝。两端共享一个constexpr规则，未来新增该命名空间武器不需要再次放宽根目录。

原生切枪/HUD资源由显式 immutable 三文件目录提供。目前仅有实际接入的 Glock/P345；左轮没有可用目录时明确拒绝，不伪报ready。每枪冷阶段3次VFS大小检查+3次generic注册，不额外读取内容、不建逐帧资源缓存，不在绘制/输入加入I/O。服务器新增这些文件后仍由实际HTTP/同步协议送B，没有手工写入客户端资源绕过下载验证。

## 已完成的构建与第一轮隔离部署

- build/cof-p345-paired-final-20261113-01/server：完整MP 3d91fceeeb570e54090a9036e139a4ce474767d8acb3facb73a5434247927fc1，广域输入稳定并封存实际链接输入。
- 同目录client-02：完整CSOLSupport 67d98c5b58cd89aa335bb04923efcf3808d76f1dc19c2f523b067545fb7c9286，548实际编译依赖与广域稳定，资源刷新开启/接口保留。client首轮CMake重生Ninja导致控制文件变化，保持失败，复用对象重新封存后才采用。
- 两模块独立冻结，PE/PDB GUID/age逐项核同；当前菜单JSON和完整资源引用配套核验通过。
- deployment-receipt.json：32项原子更新、31项完全一致跳过；独立备份，保留旧引擎/GoldCraft/AMXX/插件顺序，原参数和原环境恢复专服PID22340。未修改受保护安装、正式服务器或启动图形客户端。
- http-verification02.json：57项中52个真实载荷SHA/大小一致；库存脚本、Aurora脚本及3项P345原生HUD资源404，失败保留。第一轮配套已装不等于下载或整枪验收完成。

## 下载修复验证

| 层级 | 结果 | 收据 |
|---|---|---|
| 实际SDK冷资源/HUD注册 | 2198检查0失败；覆盖全部缺文件、每个外部回调退役、generic负值、缺实际消费者与不支持左轮目录 | build/cof-download-cold-sdk-20261113-01/receipt.json |
| 实际IFileSystem009覆盖层 | 104检查0失败，真实文件系统/PAK/读写/生命周期和新路径正负边界 | build/cof-download-overlay-sdk-20261113-02/receipt.json |
| 实际HTTP服务/Store/适配器/真实慢速Winsock回压 | 1822检查通过，范围响应/喷漆旧路径与新路径验证均保留 | build/cof-download-contract-20261113-01/http-regression.json |
| 完整引擎与GoldCraft | 构建运行中，实际进程与日志持续核对，不以暂时无输出重启 | build/cof-download-contract-20261113-01 |
| 完整MP真实HUD调用 | 构建运行中 | build/cof-download-server-20261113-01 |
| 修复后实际HTTP/单B/画面/多人/性能 | 待 | 不用组件通过替代 |

覆盖层首轮夹具遗漏实际资源缓存SourceFile依赖，已补实际生产cpp；没有替换符号或返回强制成功。首次部署未修正资源门的5个404、客户端首轮Ninja控制漂移都保留为失败。测试前后源/头/SDK/实际编译器/文件系统/资源身份逐项核验；文档可后补，不改写历史收据。

## 同时推进的左轮原生命周期证据

已用匹配独立原MP/client完整IDA分析副本核实：DLL_FUNCTIONS的Spawn4/ClientPutInServer72/UpdateClientData152/FullPack156/GetWeaponData168偏移由真实SDK编译取得。Spawn→100167D0→玩家虚槽2C→100F4090；ClientPutInServer1001D830使用同一虚槽。原玩家factory100125E0通过实际引擎分配10268字节后设置vtable/pev；零初始化和复用尚需匹配hw消费者证据。

更正：100F443D仅在global10222F14非零时，将32字节首次拔枪标志全部设为1（原xmmword101DDB00的16字节全为01），并非清零；不能无条件在每次重生清。此前“清32字节”的文字误读已撤回，未进入新增初始化代码。1001ED60将原owner pev294写到clientdata472，已建立真实网络生产阶段；准确客户端弹巢显示/恢复路径仍未闭合，不把NativeCS字段名或偏移直接套用。原证与未知在 lifecycle-review/，没有猜写生产初始化。

## 下一步与共享文档精确交接

完成正在进行的完整下载修复构建，核输入/PE/PDB和既有能力后，在零玩家窗口更新实际引擎、GoldCraft和MP；重新取得57项HTTP200/全SHA，再做单B实际下载/购买/机瞄/推搡/装填/切枪/丢拾。界面最新物理Escape停止仍有效；后台部署和专服验证不冒充界面恢复。

继续左轮真实弹巢/种子/运行准入，再VP70及全部武器；缺P/CSO动作/多人和实机性能范围保持。PROJECT_STATE/PROGRESS待补本报告层级；GOAL不缩减，TODO替换过时“P345未部署”为“P345第一轮已配套部署、5项下载缺口修复构建/全HTTP及整枪验收待”。依人类要求不发非打包跨聊消息，精确共享更新落盘待协调合并，领域目标同步。
# 2026-11-13 最终配套部署补记

完整引擎/GoldCraft 稳定构建 02 成功，766.79 秒，实际输入集合未漂移；MP 稳定构建 02 成功，77.07 秒。三对 DLL/PDB 的 GUID 与 age 均匹配，独立封存后部署，未使用被源集合漂移拒绝的首轮候选。

零玩家、B 已停止窗口中原子更新隔离环境 7 个文件，保留原启动参数、环境、地图 zs_decoy、模式 2、地图 ID 113、32 槽和 AMXX/provider 配置。新引擎 SHA-256 为 `5a4ecabc8315941fd24a84853f504e20abbaecbc2b3b270fc61ba8ef718a381d`，B GoldCraft 为 `52131e77a480cf402372da73c5ff2219b84ad5776f57bbac5d24477393fcc594`，MP 为 `b6e05eb37af9db51273641b3618a1fadf895dbcf83ca33a6dc05d609c72d90bb`。CSOLSupport `67d98c5b…` 保持。专服以 PID 42408 启动并实际回读全部目标条件；PID 仅为此检查点记录。

`build/cof-download-deployed-20261113-01/deployment-receipt.json` 保存备份、逐目标 hash、原子替换和运行就绪；`http-verification.json` 实际 GET **57/57 HTTP 200**，完整大小及 SHA 全部相同。此前五项 404 的失败记录保留。资源未人工复制到 B，故该结果证明真实 HTTP 闭包，尚不代替 B 自动下载、购买、动作、声音和多人验收。

共享文档精确交接：PROJECT_STATE/PROGRESS 将上述三 DLL、57 项 HTTP 和部署证据登记为完成；GOAL 保持全 25 身份及变体。后续单 B 结果见下段；继续左轮源码，不发送非打包跨聊消息。

## 单 B 自动下载和用户验收

人类明确恢复界面操作后，以既有隔离启动器启动唯一 B（PID 36624），没有手动复制 CoF 资源。实际入服、P345 第一人称和右侧武器名称/弹药画面可见。`build/cof-download-deployed-20261113-01/client-download-verification02.json` 按真实内容缓存与原生 `cstrike_downloads` 两条路径复核，57/57 完整 SHA 相同。首个只检查内容缓存和 loose 目录的探针漏掉原生下载目录，仅得 26/57；这是验证路径不全，失败记录保留，未据此改生产代码或补复制资源。

测试时检测到持续人工购枪、移动和窗口切换；用户随后明确回复“测试是好的接入其他”。按人工实测确认 P345 基本玩法，并继续左轮，不再重复抢占游戏输入。自动动作序列未完成，不虚报机瞄/换弹/丢拾逐项自动通过；多人、更多人物握姿和前台实机性能仍待全任务联合验收。此前 Escape 暂停已由本次明确授权解除，后续新停止仍按实际工具结果处理。
