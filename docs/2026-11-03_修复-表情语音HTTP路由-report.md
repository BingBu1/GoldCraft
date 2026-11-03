# 表情语音句子表 HTTP 路由

新客户端需要从 `sound/sentences.json` 解析人物表情声线。服务端已登记下载，但旧 HTTP 路由只允许 sound 下 WAV/MP3，导致该 JSON 返回 404。生产改动仅精确放行 `sound/sentences.json`，其他 sound JSON 和未广告文件仍拒绝。

## 验证与产物

- 实际 HTTP adapter、Store 与本机随机端口 Winsock：旧版 404（48 行为、267 引擎边界检查），新版 200（55 行为、313 边界），全部通过。
- 新版返回原文件完整 146,922 字节，逐字及 SHA 相同；HEAD、尾段 Range、大小写路由、普通 WAV 保持、未广告与 Reset 后拒绝、其他 JSON 和非法路径负例通过。
- 证据：`build/csol-emotion-http-20261103/component-01/output/result.json`。未重跑无关大字体套件。
- 完整引擎以已验 `c0d3afc1` 为基线，只替换 HTTP Server 一个对象；其余对象和库路径、SHA 保持。1 TU 编译 4.65 秒、链接 33.85 秒，6,047 输入保持、实际头/链接输入均有归属；仍使用 C++20/O3/ThinLTO/AVX2/precise。
- `build/csol-emotion-http-20261103/engine-full-01/output/swds.dll`：2,358,784 字节，SHA `e98e69dd2c627d2d9d165b2012c357aad252fc811fd7f4ffe937c5424c7cd65d`。
- 配套 `swds.pdb`：19,673,088 字节，SHA `a51037a3cc6e1313ce05156a639b7be7b978648a98af90ca2cb4d64940deb203`。PE32 x86、PDB GUID/age 匹配，两文件独立未链接。
- 可复现专项源码位于 `tests/csol-http-large-resource/sentences_http.cpp`；prepare/run 新增 `--sentences` 模式，已验证准备输出与已运行 probe 字节相同，便携入口未另行重复编译全套。

## 边界与下一步

没有运行目录写入、专服启动、图形客户端或 UI 操作。真实游戏首次下载和可听语音尚待；合批部署必须配最终人物语音客户端/服务端、WAV、服务端 titles 及未冲突的 J 绑定。此修复不证明人物技能崩溃已解决。
