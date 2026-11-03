# SKULL-2 呈现资源窄验

2026-11-03：HUD v5 与 buy v7 的静态资源验收通过。此报告仅覆盖已冻结消费者与实际资源字节，不代表完整 DLL 部署、HTTP、GPU 或游戏玩法通过。没有修改运行目录，没有启动客户端、专服或共享 IDA。

## 核验结果

- 311 个唯一资源文件：HUD 41 把武器、149 个头像、250 文件；购买菜单 35 张商品图、61 文件。原本缺失的头像 39/40 保持原状。
- 旧 HUD 40 条、旧购买图 34 条和 306 个旧同路径文件字节均保持。新增仅 `hud_v5.json`、`buymenu_v7.json`、`w312.tga`、`skull2_weapon.tga`、`skull2_ammo.tga` 五个路径。
- SKULL-2 原购买 ID 为 312，标题为“噬血狂袭SKULL-2”，购买图与 common 原 `gfx/vgui/basket/skull2.tga` 逐字节相同。
- 独立解析原 SPR 的索引与调色板，核验武器图 7,650 像素及弹药图 576 像素，重建 additive 最大误差分别为 0 与 0.322（小于半级）。没有把导出器本身作为验证 oracle。
- 重新按匹配原 PE 校验保存的 IDA 27 段、2,478 字节。原购买记录价格为 1550，mode15 的 19 条覆盖不含 312；中文 `cstrike_skull2price` 为 2200。manifest 已显式记录差异，运行价格保留原记录 1550。
- 核对 client-full-01 与 server-full-02 的冻结消费者：读取 `models/csol/hud_v5.json` 和 `models/csol/buymenu_v7.json`，服务端资源收集包含图标。后续语音合批构建须再确认最终 DLL 身份。
- 当前 B 对这 311 项为同 SHA 286、缺失 25、冲突 0；专服为同 SHA 303、缺失 8、冲突 0。该数值仅此呈现集合，不是入服总体缺件数。
- 961 个输入保持；既有 16 项技能配套部署保持。当前旧版仍需 `hud_v4.json` 和 `buymenu_v6.json`，不可删除。

## 可复核证据

- `build/csol-skull2-20261103/presentation-audit-03/result.json`：结果与消费者身份。
- 同目录 `inputs.json`：961 项输入身份及保持检查。
- 同目录 `future-resource-delta.json`：逐项源、目标、长度、SHA 与差异状态，仅准备未部署。
- `build/csol-skull2-20261103/verify_presentation_narrow.py`：独立只读审查脚本。
- `analysis/csol-model-20261017/skull-balrog11-20261102/skull2-review-20261103/price-recheck-01/`：原购买记录与语言字符串差异。

前两次审查工具失败证据保留：audit-01 为 Windows 路径大小写比较，audit-02 为保存的十六进制 token 格式解析，均在只读检查内停止、零运行目录写入。audit-03 修正读取器后通过，没有修改产品资源来迎合检查。

## 剩余验收

语音/J 配套合批后，根据最终消费者重新核部署计划；通过真实 HTTP 下载补齐 B 缺件，并在单 B 观察购买图、HUD 透明度、标题与游戏动作。上述实机项尚未完成。
