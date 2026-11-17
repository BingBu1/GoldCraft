# 建图模型扫描回归

本夹具编译实际 `native/client/precache_client.cpp`，提取实际 Renderer
`EngineForEachClientBrushModel`，只替换引擎加载边界和收集器。它验证模型表命名空间、
人物保持延迟加载、BSP／子模型、高索引、普通服回退与连接退役。

从项目根目录的 Git Bash 执行，输出目录必须不存在：

```bash
python -B tests/renderer-model-scan/run.py \
  --output build/renderer-model-scan-check \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --renderer-command build/resource-bridge-20261108/renderer-command.json
```

`--old-scan` 将枚举器替换为锁定上游 `R_LoadWorldResources` 的真实循环，
仅把世界资源收集器替换为测试收集器，要求检出失败。它不是重新写一份猜测的旧算法。

回执记录实际编译头文件、输入 SHA、提取代码和旧版来源。三处生产扫描必须都调用
枚举器，原普通实体 getter 仍执行按需加载。原生清理、错误线程、陈旧令牌、接收中、
关闭和重连均实际执行。这里没有启动游戏，也不证明整图性能、画面或多人验收通过。
