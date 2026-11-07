# ReHLDS 候选构建身份校验

`tools/Verify-ReHLDSCandidate.py` 直接登记项目中的源码，随后校验引擎 DLL、PDB 和实际链接对象。它不复制源码、不构建、不部署，也不改 Git 暂存区。

先从上一轮输入清单登记当前文件。新增的源文件或头文件必须明确加入；脚本不会把旧清单误称为完整依赖发现。

```powershell
python -B tools/Verify-ReHLDSCandidate.py prepare `
  --seed build/rehlds/Release/profile-inputs-before.json `
  --prefix crc `
  --pdb-source native/engine/crc32_ieee.h `
  --pdb-source external/ReHLDS/rehlds/engine/crc.cpp
```

`--pdb-source` 同时登记输入和最终 PDB 源文件 MD5 检查。只有需要登记、但没有调试源记录的构建脚本可用 `--add-input`。`--seed` 也接受上一轮 `*-inputs.json`，用新 `--prefix` 保留记录。默认另外检查模型加载计时涉及的五个源文件/头文件。

取得编译窗口后，用既有构建入口运行并记录真实退出码。失败时仍保留退出码和日志，不运行部署。

```powershell
$candidateTimer = [Diagnostics.Stopwatch]::StartNew()
pwsh -NoProfile -File tools/Build-ReHLDS.ps1 -Output build/rehlds/Release -MaxJobs 1 -NoPublish
$candidateExit = $LASTEXITCODE
$candidateTimer.Stop()
@{exitCode=$candidateExit;seconds=$candidateTimer.Elapsed.TotalSeconds} |
  ConvertTo-Json | Set-Content build/rehlds/Release/crc-build-status.json -Encoding utf8

python -B tools/Verify-ReHLDSCandidate.py verify `
  --manifest build/rehlds/Release/crc-inputs.json `
  --build-status build/rehlds/Release/crc-build-status.json `
  --report build/rehlds/Release/crc-verification.json `
  --test-evidence build/engine-model-profile-tests/result.json
```

检查包含：登记输入及工具 SHA 未漂移、构建成功且日志/状态新于登记、PE32 x86、DLL 与 PDB 的 GUID/age 一致、指定源 MD5 唯一匹配、链接对象集合与实际产物集合一致，以及默认关闭的资源刷新/批准观察 API 字符串未进入 DLL。MSBuild 可能重复列出对象引用，报告分别记录引用总数和唯一对象数；对象内容变化与修改时间变化也分开记录。单作业字段只证明链接器 `/threads:1`，编译并发仍由真实构建命令中的 `-MaxJobs 1` 保证。

`--test-evidence` 仅记录既有测试文件的身份，不重跑、不解释其结果。成功报告只说明已登记输入和产物的身份关系，不能替代完整依赖审计、API/ABI 测试、加载耗时对照或真实客户端验收。
