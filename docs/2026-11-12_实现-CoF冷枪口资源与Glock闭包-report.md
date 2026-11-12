# CoF 冷枪口资源与 Glock 资源闭包

本阶段补齐实际 Glock/P345 枪口资源与候选打包工具，尚未部署或启用整枪。

## 证据与实现

CoF 原 `client.dll` 为 PE32 x86，映像基址 `10000000`，SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`。原 `1004BC30` 本地枪口消费 `models/flash1.mdl`，cheap smoke 分支使用 `sprites/gunsmoke.spr`；`10090130` 世界枪口同样使用该模型，Glock type3 另消费 shell。修改前已重新检查匹配原模块、已保存完整指令和资源身份，私证位于 `analysis/cof-weapons-20261111/client-evidence/glock-cold-muzzle-before-20261112.json`。

| 资源 | 大小 | SHA-256 |
| --- | ---: | --- |
| flash1.mdl | 133896 | `3b681b535fa69397e30d49c1afd8d54a842b80f7576be5b0e9b7d1983305552c` |
| gunsmoke.spr | 214062 | `f1db5d09c80c6d628642fb9df3a308e13331280a1c2560b2928353c20985b783` |

`native/guest/cof/resource_catalog.hpp` 统一原逻辑路径与 `guest/cof` 宿主路径；服务器 `guest_cof_resources.*` 使用真实 VFS 和引擎预缓存，按地图准备十个模型/SPR ID、声音、事件、元数据和原序列时长。引擎继续负责实际外部纹理伴随文件，不新增竞争缓存。失败保留旧输出，缺文件在首次预缓存前拒绝。

`tools/Prepare-CryOfFearResourcePack.py` 新增明确的 `glock-and-shot-decals` 范围。Glock 为 44 项武器资源、5 项 shot 贴花及 1 个继承纹理伴随文件，共 50 文件；P345 为 41+5+1，共 47 文件。所有 SHA/大小 pin 属于候选准备工具；生产结构校验允许合法资源更新。原 V/P/W 几何与模型事件完整保留，Glock 十二个 Studio5004 事件按已证消费者映射七个 local sound key，不生成公共原路径声音别名。

## 验证

- 两个真实候选包共 97 个资源：189 项检查、21 种 CLI 场景零失败，81 个原资源/源码/消费者输入不变。覆盖原 SHA/大小、明确继承来源、全部模型事件、无额外别名、真实 junction 拒绝、坏 plan、二次写拒绝和中断回滚。收据 `build/cof-resource-packs-20261112-muzzle-02/receipt.json`。
- 实际 x86 ReGameDLL 冷准备：P345 689 项、Glock 787 项，共 1,476 项零失败；实际 42/50 文件及 94/110 回调边界均覆盖。原 P345/Glock 取证窗口和实际编译输入分别校验。收据 `build/cof-resource-preparation-20261112-muzzle-01/receipt.json`。
- 客户端候选额外验证了缓存枪口句柄适配：256 项效果检查及 1,187 项单一资源 owner 检查零失败；仅属候选接线，实际枪口 renderer、Studio attachment、完整 DLL 和玩法仍待。

首轮资源包测试在中文 `mklink` 输出解码时失败，保留 `build/cof-resource-packs-20261112-muzzle-01.log`；测试改为 OEM 解码，核对目标后只移除了生成 junction，本轮复测通过。未修改原安装。

只有冷准备执行文件读取、资源身份检查和模型句柄查询。后续枪口消费者借用缓存句柄；这不等于原枪口已显示或实机性能通过。完整购买/机瞄/推搡/丢拾、灾变 JSON、多人及真实 CPU/帧时仍待，当前 0 把 CoF 可玩。
