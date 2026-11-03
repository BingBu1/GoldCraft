# 失落之城 subtype 0 离线验证

验证普通求生回合的表驱动调度、180 秒期限，以及实际 SDK 地图所有者/出生点管理器。使用本机合法取得的原始 common，不随源码发布 BSP、加密表或生成后的点位数组。

本组不启动 ReHLDS，不运行真实怪物 AI/伤害，也不代替完整 GameDLL 和真人验收。`timeout.cpp` 提取两段原样方法，完整 Gate 的编译另有证据；SDK 地图组使用真实类布局和生产管理器，文件系统、实体查询、引擎分配与无关 GameRules 虚函数由夹具提供。未覆盖的 clearpoint/SDK scope 若被调用会立即失败。

## 复现

在完成项目 SDK/Clang 工具链准备的工作区根目录执行。先将环境变量 `CSOL_COMMON`、`GOLDCRAFT_LLVM_ROOT`、`GOLDCRAFT_NINJA` 分别设为原始资源根、项目指定 LLVM 安装目录和 Ninja 可执行文件。使用新的输出目录，不覆盖历史证据。

```powershell
$common = $env:CSOL_COMMON
$generated = Join-Path (Get-Location) 'build/survival-repro/generated'
$toolchain = Join-Path (Get-Location) 'cmake/clang-x86.cmake'
python tests/csol-scenario-survival/prepare.py --common $common --output $generated
cmake -S tests/csol-scenario-survival -B build/survival-repro/output -G Ninja "-DCMAKE_MAKE_PROGRAM=$env:GOLDCRAFT_NINJA" "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DGOLDCRAFT_LLVM_ROOT=$env:GOLDCRAFT_LLVM_ROOT" -DCMAKE_BUILD_TYPE=Release "-DSURVIVAL_GENERATED_DIR=$generated"
cmake --build build/survival-repro/output --parallel 1
build/survival-repro/output/csol_survival_core.exe $common
build/survival-repro/output/csol_survival_timeout.exe
build/survival-repro/output/csol_survival_map.exe $common
build/survival-repro/output/csol_survival_inventory.exe $common zs_lostcity
```

`prepare.py` 校验 BSP SHA-256 `5970511398eb588dc3949563eed5f5f42baecc976730744f0cf41335f946d56e`，直接解析其 entity lump，生成 15 个玩家点和 58 个怪物点；从生产 Gate/匹配 SDK 提取方法并输出输入/生成件 SHA。不会使用分析目录中的 JSON 作为复现前提。

`inventory` 使用实际配置解码器、怪物类型解析器和外观目录列出模型/音效/技能；它只输出清单，不授予地图运行资格。模型清单交给现有 `csol_resources` 读取器做完整依赖收集；超长 DDS 材质应经 `csol_material_stage` 生成短路径 TGA/模型材质表，再以读取器默认 63 字节限制复核。

### 地图与剧情素材补充

怪物闭包不包含地图的 WAD、天空和 BSP 实体素材，也不能代替逐图剧情素材。设置 `CSOL_RESOURCE_COLLECTOR` 与 `CSOL_DIALOGUE_READER` 为匹配构建的 `csol_resources.exe`、`csol_scenario_hud_resources.exe` 后执行：

```powershell
python tests/csol-scenario-survival/prepare-assets.py --common $env:CSOL_COMMON --collector $env:CSOL_RESOURCE_COLLECTOR --dialogue-reader $env:CSOL_DIALOGUE_READER --output build/survival-repro/map-assets.json
```

脚本校验原 BSP SHA，从实际 entity lump 取 WAD/天空/实体模型和音效，使用生产模型依赖读取器扩展模型，再用生产剧情解码器收集地图消息素材。输出只是一份补充清单，不写运行目录；仍须合并怪物、武器及公共 UI 清单。`asset-reproduction-01.json` 的 35 项与本次两份现场补充闭包一致（可选 HUD 字体另属公共 UI）。

## 已有证据

- `build/csol-season1-type0-20261102/core-03`：核心 1,851/0、原样超时方法 27/0。
- `core-05`：实际 SDK 地图 92/0；原 subtype 1 TriggerRound 回归 143/0。29 个已核对象复用，1,390 项冻结输入保持，602 个唯一实读头无未知项。
- `sdk-01`：Gate、Gameplay、Factory、MonsterServices、MonsterDamage、DeathOwner、DeathServices 七个完整 TU 编译全部通过；9,801 项输入保持，716 个唯一实读头无未知项。没有链接全 DLL。
- `server-full-01`：在已验近战/Replay 基线上组合地图与 Death 前置，69 TU 与完整 GameDLL 链接均通过；13,452 输入保持、未知依赖 0，DLL/PDB 匹配。该证据属于完整构建，尚未部署或验证真实五轮流程。
- `reproduce-generated-01`：公开准备脚本的两份生成 include 与已通过组逐字节一致。
- `inventory-02`：七图真实类型解析通过。`resources-02`：失落之城 11 模型在短路径材质 overlay 下的严格依赖复核通过，49 项准备清单，未部署。

保留 `core-01` 的缺声明、`core-02` 的解析器链接依赖、`core-03` 的 clearpoint 链接依赖及 `core-04` 的空实体查找回调失败。后者修复局限于本组夹具，按匹配 ReHLDS 的升序实体查找和 world 哨兵语义补齐。`inventory-01` 的链接首败和 `resources-01` 的缺 Pillow 环境也保留。
