# 泰迪熊 Attack2 / Blackhole2 模型事件对照

`attack_model.cpp` 直接调用生产 `teddy_model_effect.hpp`，将结果与原 MP 指令实际写出的 TE145 数据包逐字段比较。独立读取器按已核对的 HW 字段顺序读取，包含寿命与动作序号、淡出、透明度、原紧凑坐标和角度。再次经过生产 WorldEffect 编码/解码，确认宿主消息完整保留这些字段。

这组测试仅覆盖两种常规零速度模型事件，不证明完整泰迪熊 AI、原引擎动态速度全局、客户端模型逐帧渲染或真实游戏性能。完整事件 owner 的 SDK 测试与全服务端构建另有回执；不能将独立 helper 测试算作地图准入。

## 生成原包

从 Git Bash 运行工作区 Windows Python，需要安装 `unicorn`。原 MP 和私有 IDA 捕获不随源码发布。以下命令使用本项目已经验证的样本和证据目录，输出目录必须不存在：

```bash
python -B -X utf8 tests/csol-scenario-teddy-messages/attack_model_oracle.py \
  --mp analysis/csol-model-20261017/samples/mp.dll \
  --evidence analysis/nightmare-ground-20261108 \
  --dust-evidence analysis/nightmare-zero-vector-review-20261110/ground-dust-writer-bytes.json \
  --events-evidence analysis/teddy-attack-model-events-20261111/ida-capture.json \
  --output build/teddy-attack-model-oracle-repro
```

原版事件、位置 getter、wrapper 和 writer 的指令保持不变并在 Unicorn 中执行。BaseEvent 是显式 callee-clean ABI 测试边界，会修改 actor 的位置和角度，以验证重击在它返回后才读取 actor；不冒充原版 BaseEvent 的全部逻辑。共享速度向量显式输入零，其常规模板契约由独立原版引用审计支持。引擎消息回调采用已证默认 1/8 坐标和 256 步角度编码，不执行原引擎网络栈。

## 编译与比较

```bash
python -B -X utf8 tests/csol-scenario-teddy-messages/run_attack_model.py \
  --compiler "$(cygpath -w "$(command -v clang-cl.exe)")" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --oracle build/teddy-attack-model-oracle-repro/attack_model_cases.txt \
  --output build/teddy-attack-model-cpp-repro
```

编译器须已加入 Git Bash 的 PATH，配套环境 JSON 由本机已验证的 x86 工具链生成。保持 x86、C++20、O3/ThinLTO、AVX2 和 precise 浮点。直接编译项目源，不复制源树或使用 VFS overlay；只生成测试 EXE。记录实际 includes 及输入前后 SHA，要求 `success` 和 `inputsUnchanged` 均为 true。五个畸形输入子步骤须预期退出 1，正常原包比较须退出 0。

2026-11-11 已执行结果：原版 45 次事件 / 18 条数据包；生产 helper 2,752 项检查 / 零失败，五类畸形输入全部拒绝。回执分别为 `build/teddy-attack-model-oracle-20261111-02/result.json` 和 `build/teddy-attack-model-cpp-20261111-02/result.json`。源码、真实 SDK 接线、完整 DLL、部署和实机结果分别记录。
