# CoF 原枪口后端与原生持枪回调

本阶段发布独立的实际SDK适配器与原发射器。宿主第三人称合骨骼路径已修正并通过组件对照；未完成第一人称弹壳实机验收、完整接线发布或性能验收，不把此阶段当作Glock整枪完成。

## Scope、样本与证据

用户授权范围为CoF25库存项及变体；仅分析工作区副本、单B与专服。私有范围记录 client-evidence/scope.md。原client SHA256为 d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00；原MP为0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63；宿主client为b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2。全部x86／基址10000000，以下地址为VA。

E-01：原client 10052AA0写单一覆盖锁存；1008E0B8比对拥有者，1008E0DC先清锁存，1008E0E6调用10090130。类型3从真实骨骼变换的attachment1抛壳，速度为原事件向量逐轴除20，寿命2.5，声音1；attachment0发光。原指令与完整PE字节验证已保存在私有glock-muzzle-preimplementation-gate.json及关联取证文件。

E-02：本轮重新核原MP预缓存指令10120125..10120145：101E64D4为muzzleflash.sc，10120132取返回索引，1012013F存到10231F50；Glock原主攻击1004709F读此索引。weaponshoot.sc→10052E40虽有类型3独立抛壳，不代表当前Glock可达。本轮曾在进度文字中称活动事件为weaponshoot，已撤回，未据此改代码。fresh IDA和撤回说明在私有shell-owner-20261113。

E-03：匹配宿主原证 create-move-order-20261112/held：1004CDC0／1004CDC9安装人物header／model，1004D164取得P、1004D16D取header，1004D177仅SetHeader，1004D185合骨骼，1004D19C绘制。当前render model仍是人物，header已是P。原“render model必须等于P”的校验不符合真实宿主回调。

E-04：原指令差分1145场景，含Glock360场景及锁存6检查，全部零差。真实SDK适配器使用原P模型ac158045589ac76fcff9f99ce057872f8037ccdcb8f77acb6314cafca65e9d49，81检查零失败。将旧校验原样放回后，新夹具出现2失败，恢复修复源码。私有回执 build/cof-muzzle-original-20261113-01 与 build/cof-muzzle-merged-p-20261113-01；失败证据保留在后者old目录。

## Finding、实现与调用路径

F-01：severity=n/a_re；evidence_ids=E-01／E-03／E-04；confidence=high；status=validated。实际merged-P回调会被旧模型身份断言跳过。native/csol/client/weapons/guest_cof_muzzle.cpp现接受当前人物render model或明确的P render model，同时保持当前P header、weaponmodel索引、owner和单次锁存匹配。错误header／owner／阶段不消费；重复绘制不重复抛壳。没有额外偏移或强开下半身渲染。

独立适配器冷准备真实V／P模型、附件与资源代次，事件仅锁存已确认武器拥有者；实际Studio边界使用P合骨骼矩阵。local5001保留attachment0火焰与原双光。native/guest/cof/client/muzzle_flash.cpp保留原发射请求、标量运算与单锁存；补充宿主回调撤销保护，原服务回调前后检查当前代次，不在失效后继续写借用实体。临时可见实体存储保持稳定。此为宿主生命周期适配。

P-01：path_type=callflow。原MP索引（E-02）→namespaced事件接收→owner单锁存→真实人物／P合骨骼边界（E-03）→attachment0枪口、attachment1弹壳（E-01）→SDK与原指令对照（E-04）。

## 验证与未完成项

组件使用C++20／O3／ThinLTO／AVX2／precise；没有实际CPU／I/O／帧时间采样，不能把这些检查当作实机性能通过。模型与附件在冷准备解析，发射路径不读磁盘、不分配动态容器；每帧仅在pending时调用。原发射函数必须调用的引擎函数保留。

本次仅发布独立适配器、发射组件与测试，plugin／scene／CMake完整服务接线另批审计。没有部署本轮merged-P修复，未验第一人称、真实远端玩家、丢拾或整枪表现。FEV_NOTHOST＋cl_lw以及自身P模绘制阶段仍须沿原HW继续闭合，不擅自切换事件或增加本地发射器。

时间线：11月12日原发射器／锁存取证及组件→11月13日复核宿主header/model差异→新增真实merged-P检查→旧版2失败／修复版81通过→原1145零差→保留第一人称未知。完整CoFGoal保持进行。

## 后续配套与第一人称排除取证

完整运行接线随后已发布，最终客户端d2226daf经新鲜广域输入及1431实际依赖复核后部署单B；107项菜单资源通过真实HTTP与SHA。有限玩法及手模作者边界见[后续实机记录](2026-11-13_验证-CoF实机与手模几何作者-report.md)。用户确认默认右手、切枪和枪口特效已修复，不将此确认扩大为弹壳、手模或整枪验收。

E-05：继续核对同一原client SHA，1006D759以cl_lw调用引擎GetCvarPointer，1006D771保存指针到10542A74；1004F826处读取值的回调实际使用weapons/egon_windup2.wav、weapons/egon_run3.wav与sprites/xbeam1.spr，随后建立两条beam。该分支不能证明Glock本地抛壳、修改cl_lw或活动Glock走weaponshoot事件。私有cl-lw-init-ida.json与cl-lw-consumer-ida.json保留原字节及225个IDA指令；未由这个排除结论修改生产行为。

原活动muzzleflash事件的自身玩家接收／P绘制时序仍未闭合，继续沿原HW与玩家渲染入口查明。保留已证世界附件发射器，不增加猜测的视模附件、预测旗标或强制下半身绘制。此次仅补取证与文档，没有重新部署或进行新玩法／性能验收。
