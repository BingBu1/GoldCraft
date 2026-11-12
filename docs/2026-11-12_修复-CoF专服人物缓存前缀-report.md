# CoF 专服人物模型缓存前缀

这是宿主存储适配，保持原动作选择、序列顺序及标签匹配行为。原 CSO 按骨名合并和动作消费者证据延续 CoF 接入报告；本次不重新推断人物动作。

实际 ReHLDS `l_studio.cpp` 与项目 `native/engine/csol_studio_storage.h` 可在专服仅保留 Studio 模型的非像素前缀，头部 `length` 仍为完整文件长度。动作绑定若始终要求缓存跨度等于该长度，会拒绝合法人物。

`native/guest/cof/player_actions.*` 新增显式 `ValidatedCachePrefix` 范围；默认 `CompleteFile` 仍严格要求完整文件。前缀范围必须精确等于 `texturedataindex`，逻辑文件长度不得短于它，模型须有有效纹理前缀声明，骨骼和序列表须完整落在跨度内。任意截断、不精确前缀或坏表仍被拒绝。`server/player_animation.*` 仅传递这一存储范围，不改变动作映射或增加回退动作。

真实 urban、csol95、csol168 三模型的完整文件与有效前缀，逐项比较全部已绑定动作元数据及 SDK 动画状态。当前依赖下 x86 C++20/O3/ThinLTO/AVX2/precise 构建通过，10,021 项检查零失败，909 项输入编译前后不变。收据 `build/cof-player-prefix-20261112-current-02/receipt.json`；旧 `01` 结果保留，未用它覆盖后来变化的 SDK 头。

冷绑定检查序列表；实际 Select 仍为 O(1)，不读文件、不解析模型、不分配。未部署、未进行男女握姿/多人/整枪玩法或实机性能验收，CoF 可玩数量仍为 0。
