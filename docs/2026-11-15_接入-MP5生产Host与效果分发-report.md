# MP5 生产 Host 与效果分发

MP5的实际生产Host准备／角色规则／主槽库存重绑／购买身份已接通，服务器效果分发新增原身份27。该阶段不等于完成整枪交付：完整双端、独立冷服与配套资源／HTTP、游戏及多人仍待；没有部署、重启或操作用户B。

## 证据 → 结论 → 路径

原CoF hl.dll：x86／base10000000，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。修改前重核13个完整IDA函数6947B及每份原始归档SHA；包括1005B400主攻击1888B、1005A400装备135B、1005AB10人物帧1961B等。私证 `analysis/cof-weapons-20261111/mp5-client-host-20261115/PREEDIT-production-host.json`，复用原SDK／地图owner／动作和消费者证据，未以截图推逻辑。

- E1：原主攻击按Shell→清本地粒子→Gunsmoke→FireEvent27顺序，随后原后坐／震屏／动画和ShotState1、独立自动枪声。路径：`guest_cof_effects.cpp/.hpp`，原纯组件／SDK武器逻辑保持。
- E2：原Deploy1005A400固定序列0，没有TMP首拔标志；序列19收枪／20空仓机瞄来自完整原分支，客户端原模型边界处理见既有消费者证据。路径：MP5独立effects能力排除FirstDraw，允许0..20；没有改变原事件序列或替换成TMP动作。
- E3：原库存／公共reserve规则300或clip×3=90与已证MP5 owner。路径：Host设置MP5独立规则／MP5Backend，使用PRIMARY_WEAPON_SLOT、901真实类型及其W模型；绑定继续受生命／角色／实体serial和地图代际控制。CS／CSO人物动作仍是用户要求的宿主适配。
- P1：v3配置→可选MP5地图资源→真实MP5工厂和owner→角色状态→实际主槽购买／恢复→原typed事件→共用消息消费者；音频仍由已核独立native自动枪声处理。

## 宿主兼容及成本

TMP与MP5共享已核公共事件映射和实际消息分发，同时保留各自ID、序列上界、FirstDraw能力及类型。MP5枪烟／开火／准星快照均发送27；不退回Glock或TMP身份，MP5FirstDraw明确拒绝。

服务端在读图时预计算配置允许的武器位，玩家能力与其相交。旧v1／v2地图即使遇到缓存完整六枪的客户端，也不因请求不存在的MP5规则而撤销五枪；这是宿主能力协商，不是原CoF玩法。每次协商仅一次整数AND，配置遍历在冷路径；效果只增加固定类型分派，不新增热分配／读盘／网络往返。

## 验证

- `build/cof-mp5-production-effects-20261115-01`：1379实际SDK检查零失败，657真实输入前后稳定；原生消息字节、MP5Aim／ShotState1／Gunsmoke27／Shell／FireEvent27／Crosshair状态、无FirstDraw、0..20边界、跨类型和旧代际拒绝、取消后的完整包覆盖。外围引擎与消息接收端受控，未声称网络／游戏验收。
- `build/cof-mp5-production-map-host-20261115-02`：1025实际SDK检查零失败，生产Host翻译单元编译exit0，834输入前后及当前逐SHA稳定；MP5独占权限的PlayerAuthority真实函数、射击／自动枪声停止／高备弹装填、重认证保留deadline／推搡任务、实例／地图退休通过。完整生产Host初始化及购买/配置能力交集尚未执行，不能当整服验收。第一轮夹具缺少新的MP5Backend转接导致编译失败，记录保留；只补夹具调用真实runtime接口，没有回退生产代码。

编译保持Arkari x86、C++20、O3／ThinLTO／AVX2、precise与ffp-contract=off。局部固定成本不替代实机CPU／帧时。

## 剩余与书面交接

继续完整双端、独立权威专服冷启动、版本化HUD／资源／下载闭包、购买／切枪／机瞄／丢拾／技能扩容和多人；不得从Host编译推断实际运行。Glock手臂破面、全枪手模与P握姿及其他武器／变体继续；完整25身份Goal不缩。按用户要求B自测、无子代理、无非打包跨聊通信。

领域COF_GOAL同步；共享PROJECT_STATE／PROGRESS待主调应用“MP5生产Host和效果接通，源码／SDK完成，整DLL／冷服／游戏待”，GOAL保留全范围，TODO继续整枪配套和实机多人。
