# 原图集与导出图标检查

`assets.py` 检查已解出的 CSOL 挂载优先级、均匀占位图拒绝、原 SPR 调色板到 TGA 的像素贡献以及新版资源路径。它使用 Python 标准库，依赖公开的 `tools/Prepare-CSOLHud.py` 和 `tools/csol_resource_order.py`；不读取游戏进程，不部署资源。

测试需要用户合法持有的已解出 CSOL 素材，以及本项目 `sandbox` 下独立 CS 客户端的原版武器图表。仓库不包含这些素材。将 PowerShell 的 `$CsolCommon` 设为已解出 `common` 路径，将 `$StockCstrike` 设为独立客户端的 `cstrike` 路径，然后从仓库根目录运行：

```powershell
python tools/Prepare-CSOLHud.py --source $CsolCommon --stock-source $StockCstrike --output build/hud-atlas-check
python tests/csol-hud-style/assets.py --manifest build/hud-atlas-check-manifest.json
```

`build/hud-atlas-check` 及旁边的 manifest 必须尚不存在。若 common 有同级已解出的挂载目录，准备工具按已核实的 `fixtrike → locale_chn → cstrike_chn → common → fstrike → estrike → dstrike → cstrike → valve` 顺序读原件，不按修改日期选资源。非 common 的入口只查指定目录。

脚本对每项武器图标核查原帧尺寸、取图矩形、additive 模式、形状以及 TGA `RGB×alpha` 与原调色板 RGB 的差异（不超过 0.5 字节）。它不能证明实际 GPU 混合、字体、布局或最终游戏画面相同。manifest 保存实际原件路径，复核时必须仍可读取原件。

本机 2026-11-04 最终 `prepared-03` 结果为 83 个图标、421 项检查、0 失败。源图集和导出结果仅留本地。相关 C++ 资源路由检查见 [资源契约说明](../csol-skull-balrog/RESOURCES.md)。
