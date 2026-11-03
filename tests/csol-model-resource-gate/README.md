# 模型依赖识别入口回归

专项使用实际 ReHLDS SDK、完整 `csol_loader.cpp` 和资源收集器。`main.cpp` 复用已发布的 `tests/csol-dependency-cache/loader.cpp` 中明确的文件系统/控制台边界，不复制生产判断。

```text
python tests/csol-model-resource-gate/run.py --baseline <已验证缓存组件目录> --candidate <发布候选plan.json> --loader <待验csol_loader.cpp> --loader-headers <配套缓存头after.json> --common <已解包common> --output <新输出目录>
```

输入约定：缓存组件目录含 `plan.json`、`output/result.json` 及冻结 SDK/对象；发布候选为带 `files` 数组的清单，每项包含 `path`、`source`、`sha256`；缓存头清单为数组，其配套文件位于同目录 `after/`。这些工具链、原资源和构建缓存由调用方提供，不随源码发布。当前测试源码中的缓存夹具与 `metadata.inc` 会显式冻结，避免静默依赖旧私有夹具。

`--reuse-collector <已有专项目录>` 只复用成功编译且源文件、对象和实际包含 SHA 均核同的八个收集器对象；即便原轮的夹具随后失败，这八个对象仍可单独核验复用。其命令、输入和失败日志均保留。测试采用 x86、C++20、O3、ThinLTO、AVX2、precise，逐项检查实际包含来源和输入是否变化。

覆盖小写/大写/混合 `cso_` 前缀，普通及伪前缀保持跳过，真实 SKULL5 无旁挂 JSON 时的完整冷热依赖广告。实际模型需广告四项 5004 声音及 `MaleShort.tga`、`FemaleLong.tga`；广告列表与真正资源收集器结果完全比较。原资源仅从 common 读取。

追加 `--negative` 仅在测试冻结副本中恢复旧 `Q_strncmp` 门。负例应在大写模型第一次预缓存时以退出码 1 失败；结果的 `success` 表示正确观察到拒绝，不能把负例当作正向通过。生产文件不受修改。

2026-11-03 的正向结果为 488 项、0 失败。引擎 API 由受控边界替代；该测试不代表真实专服下载、客户端图形或联机验收。
