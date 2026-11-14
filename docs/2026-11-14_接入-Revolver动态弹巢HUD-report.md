# Revolver 动态弹巢 HUD

左轮原六帧弹巢显示组件已接入现有 CSO HUD 和原生备用绘制，原有静态 HUD 回归通过。实际购买、资源目录/下载准入和完整配套部署仍待，左轮没有开放购买；本报告不把软件绘制夹具当作游戏画面或GPU性能验收。

## 原证、实现和明确适配

匹配 CoF 原 client 为PE32/x86、基址0x10000000，SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`。修改前依据在私有 hud-next/runtime-implementation-before.json；十段918字节已与实际PE核同，见 final-byte-receipt.json。

| Evidence | Finding | Path |
| --- | --- | --- |
| VidInit1001BB40→1001BDD2/1001BDF5 | 原 cof_revolver_cylinder 索引及同源矩形 | 资源作者只读取已核SHA的六帧SPR，没有杜撰 mag_icon 文件 |
| Draw100177C0，100177D0/10017807 | 真实HUD对象与活动武器指针 | 采用已认证的宿主当前武器clip，不拿局部寄存器猜对象 |
| 原21分支10018C27..10018CC0 | SPR_DrawAdditive的frame是活动武器clip | `guest/cof/client/revolver_hud.hpp`、实际模块 `guest_cof_hud.cpp` 产生0..5，现代HUD/备用绘制消费 |
| CurWeapon1001C0D0→1001B150 | 有符号clip写入活动武器+A0 | 已有完整宿主弹药数保持；显示帧是内部字段，没有新增网络截断 |

原资源 SHA `1abc649e0684796805236ebd0442db4e518cfa3c400a710db27a6e09906693a0`，六个64×64独立帧。为融入已采用的CSO客串武器HUD，离线将每帧缩为既有32px图标尺寸，布局坐标完全复用；这是明确宿主美术适配，不宣称原CoF HUD尺寸为32。高于5的可信技能弹药继续显示完整数字，物理弹巢图标显示装满5个壳位；原0..5直接选择对应帧。

## 冷准备、资源所有权和绘制

目录增加可选、固定六路径 ammoFrames，逐项验证安全路径，frame0必须与既有ammo路径一致；静态目录保持旧行为。ResourcePaths收齐六帧并去重。实际HUD在冷准备中加载并校验整组32×32纹理，失败回滚新建纹理、保留未就绪状态；frame0复用base纹理，不重复上传/释放。视频/资源更换和人物/武器行清理释放其余五帧。

绘制只取已准备数组里的纹理句柄，保持真实Additive surface入口。`WeaponState.ammoFrame`为内部显示字段，默认-1保持旧静态图标；左轮模块显式设置帧。准入API接受所需帧数，六帧缺任何一项都不能当成准备成功；非法显示帧不借别的图标。现有Glock/P345准入仍要求静态1帧，左轮整枪准入/菜单循环仍待接通。

`Prepare-CryOfFearPresentation.py::revolver_ammunition`以完整原SHA为门，将原调色板、模式与六帧顺序保留到32px原生SPR，现代TGA使用相同居中最近采样；这是离线资源作者，未改原游戏安装、未部署候选，也未在绘制或输入中解码/读取文件。

## 验证与成本

- `build/cof-revolver-dynamic-hud-20261114-02/receipt.json`：601真实编译依赖前后稳定；实际HUD、目录、TGA解码器、静态武器模块注册及原生备用绘制/准入源码编译通过。现代HUD总10050检查，包含旧静态HUD9118和新增932，零失败；纯目录/宽弹药/金钱等386检查通过。
- 新增覆盖六帧/0..5及6/75/511/65535/INT_MAX完整数字、Additive32px绘制、不读资源、100次实际热绘制零new、非法帧、每一帧缺失/尺寸错误/完整恢复、整组事务和视频清理。纹理释放精确一次，没有frame0别名双删。
- `build/cof-revolver-cylinder32-20261114-01/receipt.json`：46检查稳定通过，实际六帧原SPR、全部原像素的旧解码器对照及32px现代/原生相同采样结果、两把旧图标字节兼容和坏原身份拒绝。
- 原生备用绘制与准入在本批为真实源码编译验证；夹具执行现代HUD和模块，不能据此声称实际engine SPR递送、GPU像素、客户端下载或音画实机已通过。

固定六帧只为需要该资源的行准备五个额外32px纹理，frame0共享；其余武器没有额外纹理。目录/缓存元数据定长，原生注册索引和绘制取帧O(1)，无逐帧资源解析、磁盘/网络I/O或堆分配。保留C++20/O3/ThinLTO/AVX2/precise及FP contraction off；上述软件分配检查不等于真实游戏CPU/FPS。

## 剩余和共享交接

下一步生成完整左轮购买/HUD资源候选，接实际原生资源广告和客户端所需六帧准入，再补原生命种子及实际Host/购买，完成稳定双端构建、配套下载、单B与多人/性能。全部25身份及变体范围保持，不能以这批HUD组件关闭Goal。

PROJECT_STATE/PROGRESS待同步：动态左轮HUD源码已验，10050总绘制/386纯目录/601输入稳定、46实际资源转换，未部署/未验游戏。TODO继续资源候选/准入/生命种子/实际Host购买与剩余武器。按人类要求不发非打包跨聊，此精确领域交接已落盘。

既有Glock/P345真实输入/购买准入回归另已通过：build/cof-revolver-hud-static-admission-20261114-02，10324检查零失败。01在提取当前CreateMove时发现新加入的MountWeaponCreateMove未纳入隔离夹具而编译失败；按实际mount_weapon.hpp声明补入可计数的未乘坐输入服务边界，并验证调用发生，没有给生产座机功能增加空实现。回归明确不验座机玩法。

## 完整展示资源与购买卡片（同日接续）

展示作者已增加左轮：原库存script SHA584ecbd4、原RGBA卡片93e840af和六帧SPR1abc649e均在读入时核完整SHA，使用已证脚本映射与原HUD消费者。当前native catalog的宿主898、侧武器分类1、前缀及1300价格是人类允许分配的购买适配，不冒充原CSO道具。实际Host目前仍拒绝左轮，菜单条目与资源本身不绕过整枪准入。

生成 `build/cof-revolver-presentation-20261114-01` 共13文件/173575字节，含现有两个JSON的新增行、原图购买卡片/武器图、六帧32px图、原生选枪SPR/多帧弹药SPR和描述txt；没有手动覆盖客户端、没有复制源码树。其他基线行保持，原生广告列表新增三条明确路径。较长的购买资源映射写入前记录因工具payload限制未执行，保留精确时序补充 hud-next/shop-resource-supplement.json；原IDA、运行/冷导出gate已早于修改，本次不伪造文档创建时间。

实际候选进一步验证：`build/cof-revolver-real-art-hud-20261114-04`，真实HUD总10163检查/纯目录386、615输入稳定，新增将13文件装入生产catalog/实际冷加载器，六帧TGA像素逐字节抵达上传纹理，frame选择和完整数字一致，全部所有权正确释放。03夹具遗漏csol::命名空间导致编译失败，保留后修正，不影响生产。`build/cof-revolver-shop-admission-20261114-03`：10332实际输入/目录解析检查通过，新898卡片170×80和六帧下载闭包参与校验；`build/cof-revolver-old-presentation-20261114-02`：15763检查稳定，P345及固定旧Glock作者输出逐字节一致。

完整客户端01所有代码已编链成功/SHA50b02bfa，571实际编译依赖核对通过且广域稳定，但CMake因已加入的其他领域文件重生成build.ninja，构建控制门拒绝封存；保留失败，不部署。已启动同缓存02以重生成后的控制输入再次验证，不将旧拒绝回执改成通过。

此批资源作者、菜单元数据和原生广告列表可以单独审阅；实际原生命种子/Host启用和真正购买、客户端下载、单B/多人音画、CPU帧时依然未验收。精确共享待同步：PROJECT_STATE/PROGRESS记录13资源和10163/386/10332/15763组件证据，TODO保留完整左轮闭包，原Goal全部25身份及变体保持。

## 完整客户端稳定封存

`build/cof-revolver-dynamic-client-20261114-02/receipt.json` 已成功：实际571个Ninja编译输入、广域源码及构建控制均稳定，完整链接退出0。01同次链接成功但build.ninja重生成拒绝的回执保持。02产物SHA `50b02bfa4e7ae9b8d31982d36b469b3b56a97ecf4984da66dcf3566e6d368af4`，已独立复制到该目录frozen，未使用可被编译覆盖的硬链接。封存前571源再核同，匹配PDB SHA `3f1ec84575de85e6d98ca7b1ce5c3b0b58f93e3dea12c7bfd5bfe549a7b3cfd8`，GUID3754a143-3301-4c72-4c4c-44205044422e/age1一致；详见symbol-verification.json。资源刷新配置ON保留，未部署、未启动游戏、不作运行时provider/GPU或玩法通过。

阶段已分开发布676ae93模型生命周期、e41c20d动态HUD、25c147b资源作者/三条广告/购买元数据，远端逐次核同。共享index并发期间音乐领域曾短暂提交联合暂存并重排；本任务检查自身18路径逐字匹配，未回退或改写他人历史。最终以当时HEAD完整publication清单为基线保留所有已发布条目，只添加本报告，e41c20d单独提交；完整index3947文件扫描零问题。私有核收记录在build/cof-dynamic-hud-publication-20261114-01，不发布机器/运行数据。

原生命周期新增证据：原engine ClientPut1DD7C27在调用DLL ClientPutInServer前对实际player-edict+80清684字节entvars，闭合初次连接angle为零；actual MP ClientPut1001D830→player_0→Spawn100F4090完整458指令已读取。Spawn只有survival分支填32个firstDraw为1，不把正常重复Spawn当新private calloc。普通死亡/复生的完整callee字段关系仍继续审查，尚未把未知处写成强制初始值。准确区分已证新连接/普通重生，是左轮实际Host开放前的余项。

下一步保持完整目标：闭合上述生命状态并接实际左轮Host/三枪准入/专服稳定配套构建与下载/单B整枪；随后VP70实际Owner/效果与其余全部武器/变体。P345已由用户确认正常，不重复其界面接管，真实多人和实际性能仍分别待验。
