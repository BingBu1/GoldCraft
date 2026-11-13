# CoF 手模原生加载与声音依赖

隔离专服已能加载作者手模并正常启动穷途末路。修复了两次实机启动暴露的问题：依赖收集仍要求旧声音路径，以及作者模型没有遵循原生像素尾部布局。源码、编译、资源传输和有限实机证据分别记录，完整手模画面、弹壳和整枪验收仍开放。

## 原证与宿主合同

E-01：原CoF client SHA256 d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00，x86／base10000000；HUD_StudioEvent的VA1004CAEB..1004CC44对适用手枪的5004声音执行weapons_new重写，VA10080410实际消费本地PCM。修改前映射和原IDA字节在私有studio-sound-dependency-before.json，先前完整消费者说明在weapon-effects-implementation-mapping.md。宿主的隔离PCM文件已在不可变resource_catalog中明确登记。

E-02：引擎a311d72b第一次启动因为sound/weapons/glock/glock_magout.wav缺失而被Sys_Error中止；新_hands元数据才让普通CoF模型进入完整依赖检查。原本的sound/weapons并非实际本地播放器所需路径，不能通过复制同名旧文件掩盖消费者不一致。

E-03：随后引擎fd8c3b96的原生崩溃转储显示GoldCraftEndCsolResources→PF_precache_model_I→Mod_LoadModel→GoldCraftLoadStudioOwned→Mod_LoadStudioModelPlanned的复制越界，模型为作者手模。转储SHA52b8d253cbc2d221a09b6ac08c3daeefd0ef3fc363a47f87d12d577c6f998b09。真实加载器要求描述表／骨骼／动画／几何均在texturedataindex之前，像素和调色板从该位置起连续排列。旧作者把纹理像素放在追加描述表前，促使专服回退路径读取未复制的纹理描述。根因是作者输出布局错误，不是原引擎缺陷；修改前记录native-layout-before.json保留调用栈与源码合同。

## Finding → 源码路径

F-01：severity=n/a_re；evidence_ids=E-01／E-02；confidence=high；status=validated。resource_catalog.hpp新增纯函数StudioLocalSoundResource，只在明确登记的CoF Glock／P345视模和5004事件中，将原逻辑键对应到真实本地声音资源。dependencies.cpp与引擎csol_loader.cpp共用此映射；外层原生收集器也携带模型名查询相同规则。原PrecacheModelSounds公开入口保留，无模型名时保持旧行为。未知声音／其他事件／其他模型继续原路径，缺少真正映射资源仍拒绝。没有修改原模型事件或实际声音播放逻辑。

F-02：severity=n/a_re；evidence_ids=E-03；confidence=high；status=validated。Prepare-CryOfFearHands.py先写新增纹理描述／骨映射／几何／事件隔离序列表，再追加紧密排列的完整像素与调色板尾部，回写各纹理位置及texturedataindex。原目标payload、骨架、动画、法线、UV、三角形和纹理内容保持。由于旧二进制排布有错，本次明确改变输出字节，撤回对旧错误布局的兼容；未用引擎放宽校验规避问题。

P-01：path_type=callflow。原5004消费者（E-01）→两套冷依赖收集器映射一致（F-01）→实际手模注册→标准原生布局（F-02）→真实加载器及实机启动。两个错误均先由实机暴露，再由旧版负例复现。

## 验证

| 层次 | 实际结果 | 私有回执 |
| --- | --- | --- |
| 声音／冷资源SDK | 实际loader、依赖收集、缓存与从真实sv_main提取的原生收集函数444检查通过；旧Git依赖源码从stdin编译，重现同一旧声音路径失败 | build/cof-hand-sound-precache-20261113-01 |
| 作者 | 13261检查／97供体帧；新增布局断言通过 | build/cof-hand-authoring-layout-20261113-02 |
| 真实原生加载 | 源数据／缓存保护页，专服前缀及非专服调色板展开两路径；完整依赖夹具6685检查通过。旧630b资源在同一夹具以c0000005退出 | build/cof-hand-native-storage-full-20261113-02、cof-hand-native-storage-20261113-02 |
| 语义／动画 | 旧／新资源的骨架、附件、几何、法线、UV、拓扑、纹理逐项相同；1290原通道帧全部相同，新结构审查输入无漂移 | build/cof-glock-hands-authored-20261113-07 |
| GL组件 | 已有实际适配器616检查／151角色通过新资源，执行输入SHA前后不变；不冒称本轮新DLL构建 | 同目录gl-receipt.json |
| 完整引擎 | 引擎07完整编链／广域输入前后稳定，fd8c3b9625bdafabac6e97519a22dc17c50fa4155c1bf0f711f705acea8cf36d；06其他领域源码漂移拒绝回执保留 | build/cof-hands-engine-20261113-07 |
| 部署／传输 | 新手模564f773ae0a60900784535dd38a8f1f90e61ca44546e3391d8b9a3d2b45b6524与_hands配置的实际HTTP200／SHA核同；32槽位专服正常启动，单B完成资源验证并入服 | live-server-ready.json、native-layout-resource-deployment.json |

完整加载夹具首轮缺Sys_FloatTime、完整依赖模式缺模型注册符号均在测试边界补齐；意外模型注册会显式抛错。没有把缺少边界的首次失败改成通过。原生补丁以其index记录的准确旧blob做私有Git index往返验证，实际两个函数体与生产源核同，公开补丁只加入本修复，保留其他领域未提交段。

工具代码只在离线准备执行，冷声音映射返回不可变表中的资源名，没有热路径分配或I/O。真实离线cProfile和连接边审查的成本见前份手模报告；不以冷工具时间宣称游戏FPS收益。完整引擎／SDK均保持C++20、O3／ThinLTO／AVX2和precise。

## 当前边界

旧630b资源、启动缺声音路径的记录、原生转储、旧夹具AV和漂移构建都保留。新手模已在本地部署并在Glock画面中出现，但仍有残余关节／袖口／握把视觉校准，不能以启动或GL组件通过宣称完整人物手模合格。原活动muzzleflash自身玩家的弹壳消费者仍待闭合。P握姿、地面拾取、声音／HUD、多人和前台性能，以及其余全部CoF武器／变体继续。
