# CSOL BSP hull 回归

`main.cpp` 使用真实 ReHLDS SDK 数据结构。`run.py` 根据指定引擎构建的 SHA-256 清单校验工作树中的 `model.cpp`、`world.cpp`、`mathlib.cpp`、大型碰撞壳头文件和怪物目录，从中提取加载、碰撞查询与子模型复制方法。生成的第三方方法体保存在本地 build 目录。

明确替代边界只有 Hunk 内存分配、模型索引/名称注册和测试文件读取；真实 BSP 平面、clipnode、model headnode 由生产方法加载和查询。未启动完整引擎，不代替真实STEP移动、旋转brush或逐Boss战斗。

首轮 `build/csol-bsp-hulls-20261102/attempt-01`：x86 Clang/C++20/O3/ThinLTO/AVX2/precise，编译3.87秒、链接1.14秒、运行0.35秒，2683检查/0失败；直接登记输入前后身份不变。测试消费最终 `csol_bsp_hulls.h` SHA256 `ec029100408045b3ada8c348c0e00801ba84f74d5a15fec3a68446289a567485`。

覆盖实际8张BSP：`zs_deadend`、`zs_allout1/2/3`、`zs_electron2`、`zs_multiboss`、`cs_assault`、`de_dust`；以及从原始appearance目录取得的18种大Boss bbox。包括真实entity schema与纯文本/注释/缺项/损坏的区分、默认和五图例外尺寸、旧ReHLDS尺寸及仅居中的SOLID对照、inline model共享/独立headnode、平移brush查询、普通图cvar行为和复用模型记录时的profile复位。

未来大实体仍由已有通用bbox宽度选择hull2；此生产入口不读取怪物类型、Boss ID或怪物模型名。18种已知bbox是实测样本，不表示已逐Boss验证玩法或所有地图碰撞。

2026-11-24 回归加入 `zs_evacuate1/2/3`。第三段同时检查蝗虫陨石踢落点、紧贴地面的实体边界，以及同一坐标在旧碰撞壳中的结果。11 张 BSP、18 种目录中的大型 BOSS 包围盒共 3,771 项通过。[战斗及碰撞记录](../../docs/2026-11-02_验证-CSOL通用大体型BSP碰撞-report.md)

在工作区专服地图目录准备上述地图。五张例外地图也可放在环境变量 `GOLDCRAFT_CSOL_MAPS` 指定的目录。传入一次成功的 `Build-ReHLDSCheckpoint.py` 输出，其中的 MSBuild 日志须包含 `model.cpp` 的实际 x86 编译命令。

```sh
python tests/csol-bsp-hulls/run.py --prepare build/hull-check --engine-build build/grasshopper-live-20261124/engine
python tests/csol-bsp-hulls/run.py --run build/hull-check
```

请为每次运行选择新的输出目录。`inputs.json` 记录实际项目文件、编译工具和地图的 SHA-256，`result.json` 记录编译、链接及逐图执行结果。
