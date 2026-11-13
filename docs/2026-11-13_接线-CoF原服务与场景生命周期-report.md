# CoF 原服务、Studio 与场景接线

本阶段补齐公开源码中的实际运行接线：namespaced控制、原贴花和点光场景适配、原视模／机瞄／镜像生命周期，以及匹配Renderer可选接口。首枪仍须完成第一人称弹壳、人物手模校准和多人／性能验收；没有把完整工作区构建或组件通过算作整枪交付。

## Evidence 与范围

授权为用户CoF武器移植Goal。仅工作区源码／原模块副本／隔离B与专服，Steam原安装只读。原CoF client SHA256 d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00、MP0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63，均x86／10000000。

E-01：修改前原消费者1001B4B0要求Deploy先KillLocal再清forward displacement；10071C50即时改当前视模body，GetViewModel空则不保留重放。原控制包、调用阶段和缺少服务拒绝已在client-evidence／root-lifecycle中取证。当前实际SDK控制消息2110检查通过，build/cof-control-current-20261113-01输入前后封存。

E-02：原贴花／场景链1005FF50→1005EEB0，保留可见surface、透明前的深度、原颜色空间和移动brush局部矩阵；不是在HUD尾部覆盖一张贴图。当前实际贴花／资源owner／WGL检查及原指令2476场景通过，build/cof-decals-current-20261113-01。实际Core／Compatibility像素106检查已过，当前两后端源码完整SHA重新核同。

E-03：原10090192..100901A4用renderer+40AC，即StudioGetLightTransform，变换attachment；旧插件传几何骨矩阵。本轮先保存light-matrix-before.json再更改。夹具抽取不修改的真实StudioDrawPoints函数，并提供故意不同的两组矩阵；94检查通过，build/cof-muzzle-studio-light-20261113-01。其余镜像原证见本日镜像报告。

E-04：已构建Renderer637f2066的完整输入中，本次六个场景接口／源文件与当前SHA全部相同。发布补丁只在原已发布Renderer补丁基础上加入gl_local／gl_rmain／gl_wsurf／world_leaf_build及新接口；独立Git索引重放后整棵树相同。无源码副本。未混入其他shader改动。补丁往返回执 build/cof-runtime-publication-20261113-01/renderer-patch-roundtrip.json。

E-05：将发布的plugin.cpp和scene.cpp直接从Git索引送入匹配Clang／SDK，两个实际入口语法检查零错误，未写源码影子树。全工作区客户端d2226daf已链接，广域门因Pawn／其他服务端源码变化拒绝；250个真实Ninja源输入SHA未变，完整头文件输入审计另记。原拒绝回执保留，不冒称公开HEAD全链构建。

## Findings 与实现

F-01：severity=n/a_re；evidence_ids=E-01／02；confidence=high；status=validated。独立组件已存在，但公开plugin／CMake缺少完整回调接线。新增顺序为原武器服务→控制／贴花／效果／枪口→冷资源订阅→输入／镜像→准入；视模draw scope包围原Studio，HUD_VidInit后重建资源。三种退出入口撤销就绪与对应服务。CMake加入实际TU而不复制源。

F-02：severity=n/a_re；evidence_ids=E-03；confidence=high；status=validated。原附件位置必须用light矩阵；已修真实StudioDrawPoints调用，几何矩阵仍供原绘制使用。失败阶段不消费锁存，普通枪和旁路阶段不借用CoF身份。

native/csol/client/weapons/guest_cof_control.cpp只处理专用消息：Deploy硬清本地粒子、再清forward，保留原不重置音频／aim／bob规则；body与shotstate按原即时顺序，取消时不继续写过期资源。guest_cof_particles补原KillLocal入口和资源退休接口。

guest_cof_decals.cpp连接真实引擎消息／随机／连接代次与GL owner；guest_cof_scene.cpp冷查询匹配世界和可选Renderer接口，绘制只使用同帧主视图的真实可见mask／深度／矩阵／brush记录。没有仅分配32灯就假报renderer ready。错误上下文、未就绪、修改过几何、嵌套视角等拒绝，适用边界明确保留。

Renderer的MetaRendererOpaqueScene_API_002为新增可选接口，不改已有vtable；冷生成叶可见性mask，主视角Stage暴露borrowed frame，brush只记录已提交几何及实际矩阵，按订阅开关有界4096。所有冷资源准备留在HUD_Frame／signon，普通服务器不主动读取CoF文件。

P-01：path_type=callflow。专服namespaced事件／消息→连接代次与冷资源就绪→原控制／视模事件→真实主视角Studio附件→透明前世界场景→原发射／光照／贴花；退出或资源退休撤销。E-01..05分别支撑原顺序、生命周期、矩阵和构建。

## 验证边界、成本与剩余工作

C++20／O3／ThinLTO／AVX2／precise保持；固定容量brush快照、缓存模型／附件与不可变几何，绘制不读盘、不查符号。兼容GL路径和真实framebuffer确认保留必要GL状态查询，尚无实际CPU／帧时间对照，不宣称性能完成。

本批是公开运行闭包的源码与组件证据。完整工作区DLL包含其他未提交工作，不代表此提交的完整可重建HEAD验收；本批不替换远端或生产安装。配套单B／专服、HTTP、实际枪口／伤害／多人验证继续。新手模几何候选仍有可见袖口／手指问题，不部署。所有25库存与变体的原Goal不缩小。

时间线：原版阶段取证和独立组件→本轮复核实际源SHA→真实回调light矩阵94／控制2110／贴花2476→索引入口语法与Renderer补丁往返→窄源码发布。共享连续文档更新以精确私有待应用记录保留，不发送非打包跨聊消息。

## 公开提交与完整构建补充

本批已提交a8ef902并核GitHub main同SHA；25路径的精确索引3683项扫描0问题，Renderer补丁往返树完全相同，其他shader未包含。客户端d2226daf完整编链成功；250真实Ninja源输入无漂移，742工作区头源匹配构建前清单，8个外部接口头不在原广域清单。进一步配对DLL／PDB GUID核同，288个产生代码的工作区文件逐个编译MD5与当前SHA核同。上述仍不覆盖全部仅预处理头和外部系统库，保持完整source candidate未验收、原广域拒绝不变，仅封存诊断配对。没有部署本批DLL。
