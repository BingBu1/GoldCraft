# 回合开始清理

按 2026-10-16 用户要求，`mc_round_clear_blocks=1` 的 MC 方块／容器清除和 `mc_map_mining_persist=0` 的宿主洞口／挖掘实体还原统一在下一回合开始执行。胜负提示到下一局之间保留现场。直接 `rg_restart_round`／`sv_restart` 同样走开局清理；换图、专服重启仍立即重置地图代次。

匹配的 ReGameDLL `RestartRound` 在 `REGAMEDLL_FIXES` 下先执行 `CleanUpMap`，再执行玩家 `RoundRespawn`。ReAPI 对应的两个 hook 都是无参 void 调用；`RG_RoundEnd` 是另一条可取消的通知／结束入口。生产代码继续使用已经位于实际 `CleanUpMap` 函数体内的 RAII scope，因此取消的 hook 不会发出清块代次，嵌套重入也只处理一次。没有增加结束提示、定时器或客户端猜测。

独立 `round_cleanup.inc` 保留原来的原生清理、策略失效和 `persist=1` 实体身份保护；删除结束帧提前 `RestoreMinedEntities` 和 `GameplayRules.observe_round` 两条路径。结束帧仍执行正常失效实体清扫，清块开关和挖掘保留开关在真实清理入口读取当前值。

MC 清块继续通过原协议中的累积 `clearRevision` 交付，在 MC 服务端下一 tick 应用；这不是两个进程的同步阻塞屏障。重连收到同一代次不会再次删除后来放置的方块，卸载区块在重新加载时清理。方块清理不会清除生物、已有掉落实体或修改 `mc_death_drop_items`、keepInventory、经验和主动丢物规则。

自动验证执行了真实 `round_cleanup.inc` 的入口，使用引擎／实体／物理观察适配器：36 项检查覆盖活动帧、反复结束／取消、长等待、两种 persist、热改、直接重开、同帧连续回合、嵌套清理、过期实体身份与空世界。另有实际协议实现的 GameplayRules／能力握手回归。二者使用指定 Clang x86、C++20、O3、ThinLTO、AVX2 编译通过，未降低生产优化。

旧规则测试在预期拒绝后的优化异常清理中访问了无效 Writer 地址，关闭 LTO 仍复现；将预期协议拒绝隔离到不拥有 payload 的测试函数后通过，合法／非法能力和身份负例保留。该测试调整不改变生产握手或错误拒绝逻辑。

八个既有原生／JVM 验收脚本的预期已同步为“结束保留，下一局恢复”，本轮只做语法检查，没有运行旧专服/JVM夹具。主调度已完成生产 GameDLL 构建及完整84项原生回归；真实胜负等待／重生、MC 容器与卸载区块、洞口画面／碰撞／PVS、重连去重及多人仍需主调度联合验证；领域交接后，主调度已按用户授权联合部署并重启本地双服/单B；结束等待与下一局实际放块时序仍待真人验证。独立证据保存在 `build/round-start-cleanup-20261016/`。
