# 近战 HUD 与资源验证

`hud_resources.cpp` 链接完整的生产 `client/weapon_inventory.cpp`、`client/pickup_history.cpp`、`client/death_notices.cpp` 和 `weapon_slots.cpp`，采用实际 MetaHook SDK 类型。测试边界仅替换引擎消息、命令、精灵加载与绘制回调。运行参数为已解压的原版 common 目录，不读取 NAR，也不修改原资源。

输入实际 `GeneratedWeaponResource` 输出，以原版 `knife_dragonsword.txt`、`knife_skullaxe.txt` 和 `hud.txt` 作对照。在 1280 与 480 像素屏宽下，经过实际扩展武器定义、库存和拾取消息，验证正常/选中两角色使用原卡片、近战不出现假弹药图标或数字、拾取对齐与隐藏/过期处理，以及死亡列表的原坐标、重复项保留、重置期间的指针稳定性。

2026-11-02：`build/csol-melee-20261102/hud-resources-01/result.json` 中 5 个 TU、链接与执行共 7 步通过，61 项检查、0 失败；505 个实际 include 无未知或内容变化。独立的 `collector-01` 从当前生产源编译 8 个 TU 和链接，9 步均通过，347 个 include 无未知或变化。编译均为 x86 C++20、项目 Clang、O3/ThinLTO/AVX2、精确浮点、单任务；没有完整 DLL、部署或游戏画面验收。

`resources-05/resource-closure.json` 审计 13 处实际 Precache、36 个资源与 317 个输入。旋风 SKULL-9 在未改 common 上完整收集 16 项。青龙偃月刀初次收集真实失败：缺少模型事件引用的 `sound/weapons/dragonsword_stab.wav`。诊断探针只用于枚举后续依赖，闭包仍标记不完整，不可部署该空文件。

`Prepare-CSOLHud.py` 在 `hud-export-01` 输出 239 文件；独立逐像素检查两刀原 SPR 卡片共 15,300 像素，匹配导出的 TGA alpha 和颜色量化，220 个读取输入 SHA 保持。原始近战描述符仅有一张卡片，两种选择角色保持同卡；HUD 目录两刀的 ammo/ammo2 为空。`hud-export-01-verification.json` 同时记录旋风 SKULL-9 的 16 文件独立导出与原 common SHA 对照。

两枪资源的共享委托另在 `build/csol-skull-balrog-20261102/resources-integrated-02/resource-closure.json` 验证：新 collector 对 `v_m249ex.mdl` 收集 22 项，对 `v_balrog11.mdl` 收集 24 项，合计 42 个唯一下载资源、325 输入 SHA 保持。该组只验证资源收集与生成描述符，不表示两枪已注册、开放购买或完成实机效果。前一目录的诊断脚本曾把生成文本的空 source 当文件路径，尚未执行 collector 即失败；保留记录后修正输入分类，未改变生产源码。
