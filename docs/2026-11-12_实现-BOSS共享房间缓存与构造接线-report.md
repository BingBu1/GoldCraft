# BOSS共享房间缓存与构造接线

房间查询已从旧的地图名判断改为真实 `boss_room` 查询，并按原版在行为对象构造时复位缓存。
这给泰迪熊陨石和其他需要房间范围的BOSS提供同一套入口；完整陨石/礼物盒仍未完成。

## 修改前证据

原MP：`analysis/csol-model-20261017/melee-20261102/ida-copy-mp/mp.dll`，x86，
base `10000000`，SHA-256 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`。
以下地址均为VA。父核原房间/构造链11区间427条IDA与五个新增构造前缀76条，
对应私有 `parent-room-verification.json`、`parent-cache-constructor-verification.json`；
修改前行为映射在 `analysis/teddy-meteor-room-20261112/cache-preedit.md`。

| 原版路径 | 已证行为 | 源码映射 |
|---|---|---|
| `113E6D70` → `113EA3C0` | 同一个非-1缓存id直接命中；否则按实体顺序取第一个匹配id的真实abs范围；失败保留旧缓存，输出不变 | `CCSOLBossRoomCache::Bounds` |
| `113E4940` / factory内联构造片段 | cachedId=-1，min三字段bits=`47C34F80`；不清max | `ControllerConstructed` |
| Teddy `10995960` → `10935210` → `113E4940` | 每次真实Teddy控制器构造复位共享缓存 | `CCSOLTeddyBearBehavior`构造 |
| Oberon `113D6580` → `113D65AE` → `113E4940` | 同上 | `CCSOLOberonBehavior`构造 |
| Dione `113BB410`、Revenant `113D77C0`、Light `10983E90` → `10A26590` | 各自调用共享基类构造；不是按回合或每帧复位 | `CCSOLZombiBossBehavior`构造，覆盖其实际C++派生类 |
| Meteor `10995D20`，`10995D62`、`10995DC7` | 两次烧手消息之后查询房间0；真正缺失才跳过实体创建 | `CCSOLTeddyBearBehavior::EnterTop` |

核心路径：`native/csol/server/scenario/core/scenario_boss_room.h/.cpp`、
`scenario_session.h`，以及 `server/scenario/bosses/scenario_{teddy_bear,oberon,zombi_boss}.cpp`。
原版getter对请求id=-1也只做最终id比较，保留该行为，没有自行把所有负id拒绝。

## 生命周期与成本

缓存是Session内的固定大小值成员，所有相关BOSS构造共享它；没有加入回合reset或逐帧刷新。
普通命中只读固定字段并复制两个向量，没有堆分配、模型加载、磁盘I/O或实体扫描。
新id查找沿用引擎实体顺序，不添加随机选择或由BSP文件坐标推算的范围。
源码成本分析不等于实机CPU或FPS验收，本轮没有实机性能采样。

地图令牌、busy与generation是宿主适配：查找期间若回调撤权、复用实体、再次构造BOSS
或嵌套调用，旧查询不覆盖新状态。`Unavailable`与原真实缺失`Missing`分开，避免取消被当作
“原图没有房间”而走成功fallback。调用者须保留Session；Meteor在引擎回调前明确持有它。
缺少真实Session的构造不能静默绑定一个伪缓存。

## 验证边界

- 当前完整x86服务端构建：`build/boss-room-cache-full-20261112-01/server/receipt.json`，
  2932项输入前后不变、420项真实链接输入seal，MP SHA-256
  `1a994fd96976970eb143a820f6a346e38ac571994c32e63ff74efdce975903e7`。
- `tests/csol-boss-room/sdk.cpp` 新增cache/cancellation，执行真实生产缓存、实体导出与SDK。
  `oracle.py --cache-log` 使用原getter/select和构造cache stores逐输出比较；两个基类/辅助构造是明确ABI控制边界。
- `tests/csol-scenario-teddy-bear/ground_sdk.cpp` 新增cache-owners/meteor-rooms，检查三个实际行为类构造与完整Meteor Enter。
  仍采用既有真实Session/Pool的受控模型与所选状态，不冒称原Bear模型解码或自主AI。
- 两组SDK均完成编译、链接和实际执行，success/inputsUnchanged均为true，0漂移/0失败。
  `build/boss-room-cache-sdk-20261112-01/receipt.json`：entity40/69、retirement3/32、
  cache38/136、cancellation9/35；`build/boss-room-owners-sdk-20261112-01/receipt.json`：
  cache-owners5/723、meteor-rooms62/3151。合计157行为/4146 SDK检查，不累计旧轮次。
- `build/boss-room-cache-original-20261112-01/result.json`：15键值、1Spawn、11次缓存查询、
  两次原构造reset逐输出一致；151条完整原IDA起点中实际执行146条，剩余为本夹具未走的构造引用释放分支。
  支持原id=-1的扫描/保留max特殊行为，而非按宿主习惯改成拒绝。
- 本轮没有部署、客户端启动、HTTP、真实BOSS战斗、多人或实机性能验收。

本轮源码发布先覆盖独立缓存、Session持有及已发布Oberon构造接点；
当前工作区Teddy/Zombi完整owner及其SDK大夹具仍随完整依赖审查继续，不借组件提交宣称公开HEAD已完整还原这些BOSS。

## 后续与领取取证交接

完整CMeteor与礼盒伤害/奖励继续。领取子取证已封存
`analysis/teddy-meteor-gift-20261112/child-review/2026-11-12_分析-GiftBox奖励领取闭包-report.md`：
六个原奖励记录、36领取、12实际DefaultTouch/Attach、6setter原指令案例通过；
完整入包主体当前为静态原证，HW manager前缀/记录143仍明确保留辅助证据边界。
宿主缺Janus1、JanusMK5、Balrog7三把武器本体；不能用奖励箱消失冒充武器已领取。

父另核 `11516C40..11516D90` 全119条原IDA：礼盒 `10996D2E` 发送
`weapons/firecracker_explode.wav`，channel2、volume1、attenuation bits=`3F4CCCCD`、flags0、pitch100。
该函数是声音包装，旧“震屏辅助”的措辞撤回；未据此添加猜测音效或震屏。
完整Teddy发布依赖、动态参数、死亡/回合/地图清理、rules354与实机验收仍须完成，goal保持进行中。
