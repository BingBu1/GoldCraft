# CoF 角色手模供体预缓存接线

已修复“供体进入下载列表但没有模型索引”的宿主接线，尚未部署引擎或供体资源。男女候选仍须握把、手指、袖口的真实画面校准，不把资源/SDK通过算作角色手模交付。

## Scope 与依据

用户明确要求CSO人物手臂覆盖CoF武器手臂；这是授权的跨骨架几何适配，不冒充CoF原生人物配置。原武器动画和事件保留；离线几何供体不播放自己的动画事件。原供体、目标完整SHA及显式逐骨矩阵在私有hand-precache／hands-20261112记录。

E-01：客户端ViewHandAsset在配置读取后要求EV_FindModelIndex(donor)>0；现有依赖收集器仅把供体作为generic文件，因此索引缺失会返回原手模。独立_hands.json也未进入原special门。修改前依据保存在私有typed-plan-before-20261113.json。

E-02：匹配ReHLDS源码PF_precache_model_I经过hookchain→internal，只有ss_loading可新增；先查既有索引，再Mod_ForName／AddModel。PrecacheModelSpecifiedFiles在加载期扫描模型，并在GoldCraftPrecacheCsolResources之后继续读同一header的声音。Mod_ForName可能换入缓存，故新增供体注册推迟到GoldCraftEndCsolResources，原header使用结束后执行。仅明确声明的供体主模型注册，纹理T.mdl仍是generic叶子。

E-03：生产完整csol_loader.cpp／dependencies.cpp／DependencyCache和实际ReHLDS SDK组成回归：353检查、671输入前后稳定。覆盖只有hand配置、cold／warm、批次去重、供体更换、缺失／非法路径、分离纹理、默认供体、循环配置和同一16MiB缓存预算。回执build/cof-hands-precache-sdk-20261113-05。早期SDK链接缺边界替身、非法fixture纹理和延迟阶段断言失败已保留，未改为成功。

E-04：最新离线作者3298检查、97供体动画帧保持；实际Glock1290个原动画通道帧与最新供体逐项相同。151角色真实GL适配器616检查通过。供体a0f8be1958cfdef99de364ea0b9a39c9c1336c58379ce6160ef198495b5b5a8a。候选几何尚无视觉验收。

## Finding 与源码路径

F-01：severity=n/a_re；evidence_ids=E-01／02／03；confidence=high；status=validated。generic下载注册不产生供体modelindex，导致配置存在仍不换手。

native/csol/dependencies.hpp／cpp新增WeaponPresentationDependencies，返回完整文件闭包及经过同一次JSON校验的handModels；原WeaponDependencies保留文件列表接口。没有按文件夹扩展名猜哪些MDL要变成引擎模型。

native/engine/csol_dependency_cache.hpp将两份不可变列表同条目保存，以原指纹／不存在探针一同失效，模型列表容量也纳入既有16MiB／512条预算。native/engine/csol_loader.cpp识别显式hand配置，批次积累供体名字，结束时注册；generic列表排除已计划的供体主模型，避免两种资源重复。纹理、事件声音、SEQ闭包继续完整校验。

tools/Prepare-CryOfFearHands.py追加无事件的供体序列描述，原目标字节、骨架和动画通道保留；供体不会误请求目标旧命名声音。真正武器模型的事件不改变。tests/cryoffear-hand-authoring覆盖事件脱离与动画通道身份。

P-01：path_type=callflow。用户显式手模配置→冷依赖闭包和准确供体身份→带指纹缓存→批次末PF_precache_model_I→客户端modelindex门→几何适配器按人物选择男女材质。E-01／02／03支撑接口，E-04支撑动画保持，不证明画面校准。

## 构建、部署和性能边界

csol_resources已从生产源码编译链接通过。完整swds.dll在build/cof-hands-engine-20261113-03运行，终态另记；01缺Windows环境、02未发现VS而在编译前失败，回执保留。03使用已核实际MSBuild／LLVM／SDK路径，仍直接从Git Bash启动。SDK采用C++20／O3／ThinLTO／AVX2／precise，真实文件系统和模型注册由显式边界替身控制，不替代完整引擎／HTTP／实机。

所有新增注册在冷加载期进行，没有在绘制或输入里同步读盘；缓存有界。没有CPU／I/O／帧时间实机采样，性能验收仍开放。资源暂未部署，首轮散指候选仍排除，第二轮候选待可见画面校准。

时间线：11月12日作者与骨架原证→11月13日确认modelindex缺口→typed闭包与缓存→将注册推迟至header释放后→353 SDK／3298作者／1290通道／616适配器检查通过。完整CoF25库存及变体Goal保持进行。
