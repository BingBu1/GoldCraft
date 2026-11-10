# CSOL 购买、选人和武器卡片重构

本阶段直接修改项目源码，保留 Claude 字体管理器修复。用户要求以原版 IDA 证据还原，并把所有新增和修改代码的性能作为持续硬要求；后者已写入 `docs/PERFORMANCE_POLICY.md`。当前菜单尚未部署或通过实机画面、连续点击、性能验收。

## 当前输入与资源生命周期检查点

0.1秒共享输入门已经按用户要求删除，最小F2/服务端快照检查点已推送
`24ece20084d3726235b8d94701e302f34ff21a4c`。每次F2回调立即发送一个
`cl_rebuy`；菜单内重购独立发送`rebuy_in`后关闭。测试包含同一时刻连续
17次F2以及同一时刻重新打开购买菜单，未添加新的客户端排队或计时门。

菜单消费者另完成以下原证和宿主适配核收，未部署：

| Evidence | Finding | Path |
| --- | --- | --- |
| 上表当前client身份；购买构造`181303B70`，`181304360/18130436D`建立BuySearchTextEntry，`18130438B`传DL=1，`181304394`调用`1816F1310` | setter设置`+1F4`的0x10位；实际插入`1816EDFF0`在`1816EE09F`读同位，未开时`1816EE0A8/1816EE0AC`拒绝大于0x7F的字符。因此原搜索显式允许中文；8窗IDA/磁盘原字节核同。vtable+730另写+30D，不能混作本setter | `client/buy_menu.cpp::OpenMenu`调用实际SDK `SetAllowNonAsciiCharacters(true)` |
| 匹配SDK Panel的OnRequestFocus3154、GetCurrentKeyFocus3162、SetKeyBoardInputEnabled4829，以及EditablePanel/FocusNavGroup；真实SDK旧行为反例 | 普通Panel会把原descendant继续上交，不能声称它单独阻止取得焦点。但它不能报告/释放搜索框导航焦点，Tab后仅按HasFocus会继续屏蔽R/B/数字 | `client/menu_panel.hpp`使用本地EditablePanel焦点所有者并过滤隐藏/禁用子控件；购买快捷键同时检查keyboard-enabled。这是宿主适配，未断言宿主embedded-root的闭源类身份 |
| ISurface无逐字体销毁接口；当前owner invalidator清空成功cache，After已原位更新HFont | 再次打开会重新CreateFont；资源更新后应保留成功配方，失败配方允许重试；恢复可读且新/回滚presentation已解析后才更新family键 | `client/menu_font.hpp`及两菜单的Invalidate/Layout；32轮same/change/rollback均无新增HFont |
| `InstallResourceFont`原先先AddFontMemResourceEx，再复制可分配的metadata；真实GDI分配故障测试能检出旧注册泄漏 | 把metadata复制放在取得GDI注册前，Record异常仍Remove；不改变字体配方或游戏行为 | `client/resource_font_memory.cpp`；4个分配失败点Add−Remove均0 |

私证位于`analysis/csol-menu-resource-20261110`与
`analysis/csol-menu-consumer-20261110/review.md`。稀疏IDB处以实际IDA原字节和
精确PE窗口复核，不宣称完整函数均已由IDA自动恢复。

实际控件最终回归`build/csol-buy-menu-direct-20261110-focus-03/receipt.json`：
3126检查、0失败，运行前后271输入不变；真实RequestFocus、ASCII/中文/
Backspace、Tab离焦、R/B/数字、反复开关均通过。预热Paint100、Frame100、
原生详情Paint50及Frame+Paint50的C++ new、资源读、纹理上传计数均0；冷
原生构造93次分配（EditablePanel较旧根多1次），这些计数不等于实际FPS。
后续OFF完整构建正常再生成compile_commands；该元数据的事后变化与运行内
源漂移分开记录，不把旧字节清单冒称当前元数据仍完全相同。
父另按实际runner重新推导当前三个编译命令，逐参数与focus-03收据一致；
其余270项生产、SDK、库和资源仍核同，见
`build/csol-menu-integration-20261110-08/validated-source-check.json`。

本轮角色最终完整TU回归为`build/csol-menu-presentation-20261110-18`：230/0，
166输入最终复hash不变。武器卡片最终组-13的真实生产消费为client8987/
selection273/pure376，全部0失败，34输入最终复hash不变；没有重复先前已过的
槽位menu组。卡片资源真实导出仍沿用已有身份，不将合成刀未知选中图补成猜图。

字体缓存最终`build/csol-menu-resource-fix-20261110-07/receipt.json`：959/0，
25输入不变；旧clear-all反例-08按预期失败。GDI最终-04为44/0，14输入在
运行及子代理最终复核时不变，旧Add-before-copy反例-05按预期泄漏；之后仅
resource-refresh runner补真实HUD链接输入，字体生产/探针未改，保留原收据。
实际事务探针在`build/csol-menu-refresh-20261110-01`通过108项，取消/失败
回滚/重入均覆盖；该组合随后因旧角色fixture缺宿主符号链接失败，不能称整组
通过。已修fixture；-02角色18项和旧清理反例通过，随后旧HUD链接输入失败；
补入实际纯HUD TU后-03的HUD5项及旧清理反例通过。异常退出收据均保留。

当前工作区完整x86客户端ON构建通过，2794输入前后不变，DLL SHA-256
`409d4a9888a55a9872b803931e6e375b7ccaf92a54c2e4502f35911f739c4ede`，
收据`build/csol-menu-integration-20261110-08/receipt.json`；OFF完整构建同样
通过且2794输入不变，DLL
`57df97ddc8e65269358d8f2dd9bf9e180c9c8c99063da7bff54991b1f91d9841`，
收据`build/csol-menu-integration-20261110-09/receipt.json`。
这些是完整工作区联合构建，不能当作拆分后发布HEAD已经独立完整链接的证据。

源码发布按默认OFF菜单闭包分开核收：菜单原生控件、字体包装及API声明需要
一起携带；CMake只新增字体包装TU，不混入可开启ON的option、完整资源bridge、
地图/BOSS或champion变更。ON全事务仍是另一个尚未全部发布/实机验收的工作，
本次不以其测试数宣布自动资源修复或菜单已完成真实游戏验收。原生IME、捕获
取消、多分辨率画面、重连/普通服回退和同条件帧时仍待。

## 原版身份和证据边界

所有分析均使用工作区副本；以下地址为 VA。当前 x64 模块基址均为 `180000000`，不把其偏移应用于宿主 x86 ABI。

| 模块 | SHA-256 | 用途 |
| --- | --- | --- |
| 当前 client | 0c71f6dcfda0a0c630f27e5fba1ebbcd404821811407d658a37b5206c6150538 | 原版详情、文字控件、控件样式、输入和生命周期 |
| 当前 hw | 79efdad6f8532a60a126ce29940a36154fe4f3d940922d3e17abb52d43cb20a6 | 武器表消费者、最终字形创建和字体度量 |
| 当前 vgui2 | f6a3c2098040e656769aec57232b0e94a92998f007cef9c2a5d6baf28314ba0c | 方案、边框、字体加载、原生裁剪 |
| 旧 x86 client，基址01900000 | f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd | 已交叉检查的菜单几何及 HUD 行为 |
| 旧 x86 hw，基址37100000 | c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783 | 旧 HUD 及原 HFont 不可重配置行为 |
| 当前 GameUI | bd31fdf1c4ee795bf24de64ab28a6fcf0671c86db485ff3d563cbd505aa48770 | 排除购买预设编辑器与实际 B 菜单的混淆 |

当前 GameUI 的 `1805B52C0 / 180513120` 是预设编辑器，实际购买与选人消费者在 client。旧 x86 vgui2 仍缺失，因此当前固定字体算法不反推为旧版本事实。原方案 `resource/clientscheme.res` SHA-256 为 `308f479fa407df14e69e811e066dcc2d1f35aa8ee9f717d2344ab603e08e93d9`。

私有原始反汇编、字节、资源与模块保留在 `analysis/csol-menu-client-20261110`、`analysis/csol-menu-hw-20261110`、`analysis/current-cso-menu-20261110`；不随源码发布。父已独立核当前 client/HW/VGUI2 身份、详情相关150窗、滚动条20窗及字体32窗的磁盘哈希。字体32窗中31窗与 IDA 原字节相同，1窗是已说明的 loader 清零 IAT；原导入描述符、OFT 和名称另核，不宣称32窗逐字节相同。

## 证据到源码

| 已证消费链 | 实现及边界 |
| --- | --- |
| client `01DE6070 / 01DE5FE0`：640×480、宽屏根留白、X/Y分别缩放和逐层截断 | `native/csol/menu_layout.hpp`；两菜单和命中共用。基础已发布 `17d68e8`，289项布局、286项输入检查通过 |
| 当前 `181307150 / 181307318 / 1812E58A0`：详情初始隐藏，悬停显示，退出不自行隐藏 | `native/csol/client/buy_menu.cpp`；不猜初始第一行自动展开 |
| 当前 `1812FFB20 / 181300439` 等：属性表和说明互斥 | 购买详情原生子面板与 RichText；未知 table 保持可表达，不伪造分支 |
| 当前 `1817096F0 / 18170E820`：RichText 比例内边距和完整高度 | 匹配 SDK RichText 宿主适配；内容、字体或视口变化才重新排版，先解决宿主延迟换行再测高 |
| 当前 `181708C30 / 181708DDB / 1817088D0`：画布宽减10字面像素、12px外滚动条、动态步长、顶部复位 | 原生滚动、输入和裁剪；不使用固定10滚轮步长 |
| 当前 `18175D660`：原图滑块、12px端帽，短滑块端帽高度减半但底端仍从 end−12 开始 | 保留原图/原分支，未用宿主平色滑块或自行对称修正 |
| 当前 `181712B90 / 181712BD0 / 181712C70 / 181712D40`：箭头捕获、无焦点请求、布局差异 | 保留原按住滚动；箭头 inset `(1,±1)`、slider x=2、按钮宽 W。`menu_panel.hpp` 仅是宿主合成层适配 |
| 当前 VGUI2 `180030B10 / 18002F870` → HW `18076A9B0 / 180D81660 / 180D83220` | `menu_font.hpp` 独立 `GetCSOScaled`，两菜单消费。当前原版无 yres 配方固定用768/480并上限128，无语言最小字号；实际 GDI 度量单独读取 |
| 当前 `1813CF890 / 1813CF913` → `181701390 / 181700940` | `characters.cpp` 缓存实际字符断行、CR/LF/ampersand规则及 glyph 位置；原生 root→helper→label 祖先裁剪 |
| 当前 `1816B30D0 / 1816B2E30 / 1816B4DB0` 与 helper refresh；`1813D1A70`→资源同步应用→raw bounds复制 | 已闭合Scenario说明额外行距0；分页资源先应用再复制最终ClassInfo边界，保留原资源高380，33/33新IDA字节窗核同 |
| 当前 `18130B754 / 18130B75D`、Favorites `18130B367 / 18130B370`；默认值及跨HW链 | 普通mode15默认立即发送后同栈关闭，不以服务器结果锁住界面。运行态篮子override明确另有分支，不冒称已完整实现 |
| 已匹配 client/MP/HW 的武器卡片与声音消费者 | `client/hud_style.cpp`、`client/weapons/weapon_inventory.*`、`hud_style.*` 和 `Prepare-CSOLHud.py`；普通服务器仍走原生 HUD，待完整实机视觉/输入验收 |

当前字体提交配方 `Default9 / Title12 / tab8 / Marlett11` 为 `14 / 19 / 12 / 17`，这不是保证所有 Windows 字体的实际像素高度恰好等于该数值。保留 `GetHostScaled` 作为明确区分的旧宿主适配，不冒充当前原版算法。

## 资源与性能

所有资源读取统一解压目录，不重新扫描 NAR。购买 buy-05 导出97文件、6,282,478字节，43个基础物品原表名字/说明、独立最大装弹量、实际属性顺序、grade及三段原滚动条；97个文件哈希均核同。角色 characters-02 导出47文件、151说明、无缺项。HUD资源组-03为43武器、149头像，未知两把合成刀卡片的选中图消费者仍不补猜图。

颜色查找原来每100次 Paint 有100次 C++ 分配，原因是长键 `brightcontroltext` 被隐式转成临时 string；共享颜色表现改用透明字符串查找。原图、字体、文本和 glyph 预备放在加载或状态变化路径；稳定帧不重新读取资源、解码、上传纹理或排版。实际游戏 CPU、帧率和点击响应仍待同条件采样，不能把离线计数称为实机收益。

## 编译与自动化

- 布局/输入基础：289/286项通过，已提交推送 `17d68e80e3e5b08d85fdcfeb1474cbf8c1486e1c`。
- 字体：`build/csol-menu-font-cso-20261110-03/receipt.json`，157项通过，25输入不变；预热1000次分配、重复字形注册、屏幕读取均0；实际字体高度查询独立计数。
- 字体7路径已提交推送 `e7b20e41280a2971b31047cf9c301ec13a6b0c8d`；卡片资源契约和输入状态9路径已提交推送 `d5f834feee5943ff4681d6cbe61170e76312ff51`，3234项精确index扫描0问题，远端SHA核同。绘制消费者尚未随该窄提交发布。
- 菜单数据：`build/csol-menu-data-20261110-04/receipt.json`，真实生产解析器消费 buy-05/characters-02，257项通过；8输入独立复hash不变。共享解析/导出10路径已发布main `7878ea90affd75c47357db0eeeb0d21d646e6bd6`，3237项精确index扫描0问题、远端SHA同。
- 购买完整生产 TU 初步组-09：1081项通过，稳定 Paint100 / Frame100 分配、读取、上传均0；此组未构造新原生控件，不当作原生控件通过。
- 购买原生控件最终组：`build/csol-buy-menu-direct-20261110-native-09/receipt.json`，1255项通过、269输入不变；真实SDK悬停、祖先裁剪、滚轮、箭头捕获与拖块、样式和清理均覆盖。稳定Paint50与Frame+Paint50分配／读取／上传全0；之后普通购买关闭改动须另做最终状态回归。
- 角色完整 TU 最终组：`build/csol-menu-presentation-20261110-17/receipt.json`，230项通过、165输入不变，覆盖当前固定字体消费者及真实SDK祖先裁剪。旧组-15仅保留历史。
- 武器卡片组-12：17步全部通过（client8987、selection273、pure376、menu153）；真实资源组另11291项。属于该输入版本，最终联合构建与游戏另验。
- 完整当前源双端构建已成功，2787源输入前后核同：客户端刷新ON，`CSOLSupport.dll` SHA-256 `866699af3273dc3912acd3ce974ed9fd6b49970e0dfa3bdff3273d4d930202f5`；`mp.dll` SHA-256 `d0cc60778e951ac83e04f5bd403a6cc40e1d4cb6f4229ea3474089703d73473c`。收据分别为 `build/csol-menu-integration-20261110-01/receipt.json` 及其 `server/receipt.json`。之后普通购买关闭源码已变化，最终客户端增量构建已通过：`build/csol-menu-integration-20261110-02/receipt.json`，53.06秒、2789输入不变，3,691,520字节，SHA-256 `f0d1742d4d12f017abbafbccc694e846ad4bb326409be322c36407c91ae14ad1`。并行地图修改使此前服务端来源过期，须重新完整构建；服务端同样解析购买JSON，部署必须配套。

## 撤回、未知与下一步

曾将普通 helper 构造期400×200误当作 paged RES 最终高度，已撤回；最终factory、资源同步apply与十个helper/fallback的覆盖顺序已经闭合，实际原paged `ClassInfo` 为 `(263,50,400,380)`。生产继续保留资源边界；raw380不等于380屏幕像素。详见私证 `analysis/csol-menu-hw-20261110/2026-11-10_reverse-character-lifecycle-closure.md`。

短滑块底部初步写成 end−cap 是错误推导，已在新原字节证据后改为 end−12，未部署。测试能编译不代表还原正确。

购买mode15默认关闭闭包113窗、默认／跨模块／篮子控件47窗已核身份与原字节。`+220`是篮子，收藏另为`+228`；manualclose在mode15隐藏，不能因其cvar猜自动重开。生产普通购买已改send→Close，普通权威回执与UI独立；F2已改由服务端实际装备快照一次重放，菜单内独立rebuy_in发送后关闭；不再用客户端历史逐项排队。宿主回执用固定128KiB的16位token→item表，无新UI队列、锁或猜计时；完成、token重用、会话重置清理。该表是宿主协议适配，不能把它说成原CSOL内部实现，完整原版运行态篮子和自动购买仍未还原。最终状态native-10已通过：2866检查/0失败，269输入独立复hash均不变；延迟/错配/重复回执、快速重开、乱序、背景重购、重置与token重用均覆盖。F2后续原证已闭合实际快照、固定购买顺序及独立paint尾，并按用户要求删除原0.1秒间隔；最新客户端2913/0、当前工作区双端完整构建及服务端16组97行为/15181检查已过，实机仍待，详见docs/2026-11-10_实现-F2服务端装备重购-report.md。

人物Scenario说明在已核生命周期中的line-extra0已经闭合，setter确实存在，不能泛称TextImage永不改变。非灾变／旧路径、RichText非打印且非控制Unicode分类差异、个性化物品分支、两合成刀选中图仍有明确边界。

完成原生回归和双端构建后，继续单B实际画面、多分辨率、搜索/焦点、连续点击、跨图/断线与性能采样。不得部署未闭合菜单 WIP，不重复已验收的7键卡顿测试，不中断真人对局；完整大灾变 goal 仍 active。
