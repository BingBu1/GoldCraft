# CSOL 绝命档案位移与速度原指令对照

使用匹配 SHA-256 的原 x86 MP，以及在修改前已保存的 IDA 完整指令边界，实际执行 `108595D0..108597FC` 和 `108588E0..10858975`；逐位比较生产核心的体力、请求／实际跑动标志、历史坐标、最终速度和 `sv_maxspeed` 查询次数。

```bash
python -X utf8 -B tests/csol-standalone-movement/run.py \
  --original analysis/csol-environment-implementation-20261114/samples/environment-mp.dll \
  --ida-evidence analysis/csol-standalone-stamina-20261115 \
  --compiler "$GOLDCRAFT_LLVM_ROOT/bin/clang-cl.exe" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/csol-standalone-movement-new
```

输出目录必须是工作区内的新 `build` 目录。原指令私有测试适配器仅生成于该目录，不能发布；生产源码始终在 `native`，输入哈希在前后核对。编译使用 x86 C++20、AVX2、precise FP、O3／ThinLTO。

原函数的玩家解析、native ResetMaxSpeed 和 CVAR 是显式边界；ResetMaxSpeed 回调进一步执行完整原速度窗口。噪声截止时间等于当前时刻，单独的 `makenoise` 服务不在本夹具覆盖范围，任何意外噪声、异常数学或不变量回调直接失败。不能将本组件对照当作完整疾跑、PM、预测、碰撞、多人或实机性能验收。

位移扣费使用原值 `0.1`／地图单位，不能直接使用历史 CoF 的“每秒消耗”设置。新跑动请求首先快照当前位置；蹲姿保留原实际跑动标志；松开请求不推进旧坐标；扣尽后清除实际跑动，随后使用行走速度。调用顺序和浮点中间舍入是测试的一部分。
