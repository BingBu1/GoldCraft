# CoF原生移动输入适配

已接真实匹配客户端的原生输入消费者，保留原绑定、按键幅度和其他插件的回调链。完整客户端、实际SDK消费者和版本目录检查通过。**服务端尚未发布输入策略，CofSprint仍不可用；本阶段没有部署，不代表奔跑、体力权限或双端预测已完成。**

## Evidence → Finding → Path

宿主client为x86/base10000000，SHA256 `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`，CRC64/XZ `124D6034A6B28B40`。原CoF client为x86/base10000000，SHA256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`；原CoF hl地速模块SHA256为 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。不同模块的地址没有混用。

修改前合同和宿主全函数IDA结果在私有 `analysis/cof-movement-native-20261114`。本轮重新取得完整337指令，宿主四个字节区域核同，其中回调槽4字节属于加载器零初始化区域，不能当作磁盘中的有效函数指针。原CoF输入与PM证据复用前重新通过29窗口、799指令、3140字节核对，记录在 `analysis/scenario-custom-rules-20261113/time-stamina/continuation/verified.json`。

| Evidence | Finding | Path |
| --- | --- | --- |
| 宿主CL_CreateMove1005B320..1005B8ED；1005B6FC调用101179D8，1005B71D..7B0限幅，1005B7B5..7EF处理+speed | 原CS先按武器速度截断命令，再乘cl_movespeedkey；在函数返回后反向放大无法恢复斜向和部分按键幅度 | `client/scenario/core/cof_movement_input.cpp`只在有效权威策略下包裹真实原生输入函数 |
| 宿主1005B81A调用1005A3C0组装buttons；速度按键入口1005AA50/1005AA60与已核原输入命令链 | 原CS的+speed不产生CoF需要的IN_RUN；原生双键和手动释放所有权必须保留 | 原+speed/-speed继续执行；额外观察两键，输出冲刺意图；菜单、死亡、失活和生命退役清理旧意图 |
| 原CoF client1007A880入口、方向消费者和10079EDA..10079F3E注册；原hl的raw3D→百分比→最终上限 | 方向参考不是最终移动速度。原注册forward为230，当前CoF安装配置为300/back200/side200，不能把安装配置冒充模块默认值 | 自定义26字节CsoInput传权威方向参考，不在客户端硬编码配置；原up轴保持原生值，实际PM接线仍待 |
| 精确SDK的ResolveGameSymbol和匹配目录协议 | 只能使用当前模块对应的函数、回调操作数及运行时非空槽；版本不匹配不能猜地址 | `native/client/cof_input_10210.json`与`tools/Build-ClientGameData.py`核全SHA、CRC、完整函数SHA及调用操作数后生成两个符号 |

调用路径为：有效当前生命策略 → 原生CL_CreateMove按键幅度/视角 → 原始方向命令和IN_RUN意图 → 后续实际服务端PM与同源预测。最后一步尚未接入，因此没有启用策略发布或提前开放管理员能力。

## 宿主适配与生命周期

只在原生调用的作用域内借用F/B/S数值、cl_movespeedkey=1和速度回调；不改CVAR存档字符串、配置文件或按键绑定。临时速度回调返回0，目的是将raw限幅留给后续真实PM消费者，属于明确的宿主适配。普通服务器、无策略或关闭路径继续原始输入。

单个作用域保存当前回调头，结束立即恢复；回调期间其他插件的新写入保留。最初候选每次把当前头重新设为回退函数，外层插件若保留我们的临时回调，第二次进入后就会递归。新增实际SDK回归先复现旧实现失败，随后改为每个已解析原生绑定只捕获一次稳定回退，当前头独立保存；覆盖第二次输入、关闭、重新初始化和原生回调内关闭。失败证据保留在 `build/cof-movement-input-opaque-old-20261114-01`，旧候选未部署。

InitHUD、连接重置和生命替换更新代次；若原生调用中发生退役，旧命令清零，不能进入新生命。作用域内关闭会先恢复借用字段，再移除本插件的原生hook。不可遍历的外层插件回调不被强行拆除。外部线程不借用owner状态，嵌套调用不重复覆盖字段。

## 验证

| 层次 | 本阶段结果 |
| --- | --- |
| 实际生产消费者、匹配SDK | 65项通过，覆盖半幅/斜向、原绑定、双键/手动释放、UI/死亡/失活、旧生命、外部写入、嵌套、外层回调及关闭重入；最终回执 `build/cof-movement-input-consumer-20261114-05/receipt.json`，实际484依赖稳定 |
| 版本目录 | 13项通过，实际生成62记录；SHA、CRC、函数、槽操作数和调用字节五种错配均在输出前拒绝。`build/cof-movement-input-gamedata-test-20261114-03/receipt.json` |
| 完整客户端 | `build/cof-movement-input-full-20261114-02/client/receipt.json`成功，584实际编译输入及广域输入稳定；C++20/O3/ThinLTO/AVX2/precise配置保持 |
| 封存候选 | DLL SHA256 `0b10a3b3c11e93186b26c047e16e34a1213825119fb7f4a0ff0988798355df5c`；独立DLL/PDB位于 `build/cof-movement-input-sealed-20261114-02`，没有复制源码树 |
| 实际DLL注册 | 匹配最终PDB，加载、卸载和重载2215项通过；`build/cof-movement-input-dll-registry-20261114-02/receipt.json` |
| 热路径成本 | 1000次实际适配调用零new、零CVAR/符号查找；协议固定栈值，无I/O或增长缓存。这是受控SDK成本约束，不是实机FPS证明 |
| 部署与玩法 | 本候选未部署；服务端无CsoInput发布，CofSprint仍不可用。未验真实移动、玩家碰撞、水梯、冻结、多人及游戏CPU/帧时间 |

SDK夹具执行实际生产适配器，但其下游命令生成器是按IDA顺序编写的明确模拟边界，**不是完整原client.dll执行**。地速数学的原x86逐位对照24597项见[上一阶段报告](2026-11-14_接入-CoF地面速度原指令对照-report.md)，两种证据不能互相替代。早期输入回执复用了环境测试的边界说明，已在runner按实际夹具区分，最终05回执正确，旧记录未改写。

复验命令（项目根目录的Git Bash，输出须使用新目录）：

```bash
python -X utf8 -B tests/cof-movement-input/run.py \
  --client-build build/csol-client-crc/client \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/cof-input-recheck-new
python -X utf8 -B tests/cof-movement-input/gamedata.py \
  --client analysis/cof-weapons-20261111/client-evidence/host-client/client.dll \
  --output build/cof-input-catalog-recheck-new
```

下一步按原CoF PostThink及真实PM顺序接服务端体力许可、raw/final限幅与同源客户端预测，保留GoldSrc实际hull/playerclip、水梯和状态速度所有权。完成前不开放奔跑能力。永夜四文件部署保持上一阶段已验版本，最终画面仍受后续实体Escape停止影响；本报告不把部署或离线检查当作实机验收。完整Cannon→大灾变goal继续active。
