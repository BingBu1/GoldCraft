# MP5 原图 HUD 与购买资源

MP5A3 的原图卡片、独立弹匣图标、NativeCS 三文件闭包及 CSO HUD 模块已在项目源内接通。原图作者、实际 HUD／购买解析和六枪冷资源验证通过；生产 Host 回调仍未安装，不能购买整枪。本轮没有更新 DLL、重启专服或操作 B；用户继续自行测试已安装版本。完整25库存身份与变体 Goal 保持。

## 证据 → 结论 → 实现

原 CoF client.dll，x86／base10000000，SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`。本轮重核完整 VidInit1001BB40／1105B 和已存 HUD2716B 窗口，均与原PE逐字节一致；修改前证据保存于 `analysis/cof-weapons-20261111/mp5-client-host-20261115/PREEDIT-hud-assets.json`、`PREEDIT-hud-implementation.json`，新IDA正文见 `hud-consumers-ida.json`。

- E1：1001BCE1推入cof_mp5_mag_icon，1001BCE6调用1006CE40，1001BCF5存字段+C8；原ID27在100191A7分支从相同+C8取图、100191EA绘制。结论为MP5独立原图静态弹匣。路径：`native/guest/cof/presentation_resources.hpp`、`native/csol/client/weapons/guest_cof_hud.cpp`、离线作者。
- E2：原weapon_mp5库存元数据SHA917fd1e6…实指mp5/640_mp5_slot.tga；原TGA SHA38be4519…、原弹匣SPR SHAbabb08a0…均固定全SHA。结论为MP5A3自己的卡片和弹药像素，禁止借TMP图片。路径：`tools/Prepare-CryOfFearPresentation.py`。
- P1：原图→离线完整SHA校验／转换→版本化JSON及NativeCS清单→真实HUD解析／纹理owner→901固定模块；共用准星仅注册一个绘制回调。

CSO布局／170×80购买卡片与170×45切枪图转换属于宿主适配，不冒称原CoF布局。host901／冲锋枪分类3／价格2400和灾变伤害仍为用户授权的自定义元数据；没有更改已证原武器行为。

## 实现和兼容

新增固定三文件原生HUD清单与MP5静态模块；没有增加热路径查表、资源读取、网络请求或独立重复准星。资源作者输出8件约149317B，保留baseline全部既有HUD／菜单记录。自动枪目录从单一result.back变为显式identity索引后，作者解析现覆盖两种写法，并验证精确MP5／TMP身份；未放宽到任意枪型。

工具两次遇到WinError5原子目录提交失败，分别保存在previous-art-01／02，不当作通过。现仅对Windows共享／访问错误5／32／33重试同一次目录原子重命名，最多六次、等待上限1.55秒；每次确认目标不存在，永久拒绝仍抛错，不覆盖已有候选。实际原图生成和注入临时／永久失败／目标已存在三种边界通过；属于离线工具可靠性适配。

## 编译与自动检查

| 回执 | 结果与边界 |
| --- | --- |
| `build/cof-mp5-presentation-art-20261115-02` | 14848检查，8件真实原图输出；旧记录保持，原弹匣逐字节、RGBA／原图卡片、NativeCS量化与六枪最小闭包通过；原子提交故障分支通过。 |
| `build/cof-mp5-previous-art-20261115-03` | 15290检查，TMP输出与原图保持；四旧手枪全部输出与原作者逐字节一致。只生成数据候选，没有复制源码树。 |
| `build/cof-mp5-presentation-sdk-20261115-01` | 真实HUD10661、纯386检查零失败；611输入稳定，实际注册／目录／纹理消费者、MP5图标像素、clip到INT_MAX／reserve500、100热重绘零分配／读盘及video资源清理通过。 |
| `build/cof-mp5-presentation-admission-20261115-01` | 10479实际SDK检查过，实际菜单／HUD解析与输入编排；六枪能力63、缺MP5 HUD退回31而其他五枪可用。外部服务状态受控，不作为整枪玩法。 |
| `build/cof-mp5-presentation-cold-20261115-01` | 六枪4222实际SDK检查零失败，739输入稳定；18件真实原生HUD资源在任何缺件／取消／登记失败边界均按现有事务处理。非完整可选HUD部署验收。 |

SDK继续Arkari x86、C++20、O3／ThinLTO／AVX2、precise与ffp-contract=off。局部100次重绘无分配／读盘不等于实机CPU／帧时通过；没有减少效果或跳过资源校验。

## 剩余与交接

下一步安装MP5真实生产Host准备／事件／角色规则／主槽库存、购买与丢拾全链，原MP5没有TMP首拔语义；再做完整双端／独立冷服／全HUD下载闭包和用户授权的部署。手模／Glock破面、P握姿、多人和实际性能仍待，完整goal未完成。B由用户自测，未重新请求接管或操作。

领域COF_GOAL同步。共享文档准确更新待主调应用：PROJECT_STATE／PROGRESS加“MP5原图HUD／购买资源和实际SDK已验、生产Host／整DLL与游戏待”；GOAL保持全部25身份和变体；TODO下一步为MP5生产Host整枪及手模／多人／性能。按用户要求不发送非打包跨聊通信。
