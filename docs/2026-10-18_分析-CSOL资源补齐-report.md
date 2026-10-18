# CSOL 完整 common 资源库与文件名读取修复

用户指定资源库已补齐：从原 25,202 个文件增加到 66,604 个文件，共 14,261,150,228 字节。新增 41,402 个文件、7,106,420,778 字节；原有 25,202 个文件逐项 SHA-256 保持不变。新版中文 HLAM 已更新到用户指定的工具目录，原 INI 保持。

本阶段完成本机所提供来源的素材整理和查看器路径修复。CSO 原游戏逐帧一致性、其他拆分模型的客户端逻辑，以及既有未完成的材质和世界事件功能仍未验收。

## 范围与来源

读取用户已解包的 PAK `common`、同级 `locale_chn`，以及客户端 Data 中六个 NAR：fstrike、estrike、dstrike、cstrike_chn、cstrike、valve。没有修改游戏安装、源 NAR、源 locale 或 GoldCraft 运行文件。所有资源与机器路径仅保留本地，公开交接仅含查看器补丁和本文。

保留原 common 的全部同名文件。缺失路径优先使用已解包 locale PAK；其他缺失路径按 NAR 内条目时间选取较新版本，同时间沿用查看器的确定性包顺序。这是本次整理规则，不把解包文件时间或本次回退规则宣称为所有 CSO 版本的实际挂载规则。

六个 NAR 共 42,951 条、40,593 个不区分大小写的独立路径。741 个路径被原 common 保留；其余 39,852 项先解出，共 6,976,531,337 字节。随后合并 locale 的 3,348 项：新增 1,550 个路径；1,644 项与 NAR 内容相同；154 项以 PAK 版本替换本次导入的旧 NAR，替换前校验目标仍等于本次导出的 hash。原 common 没有任何替换。

## 证据

下表路径均相对本地领域证据目录 `analysis/csol-model-20261017`。全部资源属于本机研究输入；重现实测需要同一合法持有的源资源，公开源码不带商业素材。

| ID | 观察与证据 | 验证层级 |
|---|---|---|
| E-001 | `common-before.json`、`nar-common-preflight.json`、`common-merge-plan.json` | 原 common 全量 SHA-256、NAR 原始索引和完整补齐计划 |
| E-002 | `common-merge-results.jsonl`、`common-locale-results.json`、`common-merge-verification.json` | 新增文件逐项来源/hash；66,604 文件全量回读；六个 NAR 与 locale 源树重新校验 |
| E-003 | `resources-korean-independent.json`、`resources-korean-nar-result.json`、`resources-korean-loose-result.json` | 22 个真实韩文名 BMP 独立解码；两种命名表示在 NAR/目录上的 88 次内容对照 |
| E-004 | `common-independent-decode.json`、`common-reader-result.json` | 113 项、170,721,469 字节的 Python 独立解码；实际 HLAM 135 次资源读取 |
| E-005 | `resources-build.log`、`resources-ctest.xml` | Clang/C++20 x86 生产构建与 712 项格式、目录和边界检查 |
| E-006 | `gl-resources-parts01/result.json`、`gl-resources-events01/result.json` | RTX4090 实际 GL/Qt；组合模型与 Draw 事件；无补充游戏目录时导出/离线重开画面一致 |
| E-007 | `resources-package-export-verification.json`、`package-resources-smoke01/result.json` | 两组 183/197 资源全部来自 common 且源/导出 hash 一致；最终包真实 GL/Qt 运行 |
| E-008 | `hlam-source-replay-resources.json`、`hlam-source-scan-resources.json`、`hlam-delivery-resources.json`、`user-tool-resources-deployment.json` | 129 文件累计补丁回放/公开内容扫描、ZIP CRC/hash、40 文件部署核验及 INI 保持 |

## 发现与修复

**F-001（已验证，E-001/E-002）：PAK 解包目录不含全部基础素材。** 原 common 与 NAR 的交集仅 741 条，遗漏包括旧模型、外部模型贴图、SPR、环境面、声音、地图和界面资源。按相对目录补齐后保留 `models`、`sprites`、`gfx`、`shader`、`sound` 等结构。用户目录旁另保存完整来源/hash 清单与中文说明。

**F-002（已验证，E-003/E-004）：ASCII 名称转换会破坏 NAR 解码。** 22 条路径包含 CP949 韩文。旧参考库把高字节替换成问号，并用替换后的路径生成密钥，实际报字典距离越界。提取器使用原始路径字节计算密钥、检查存储 CRC、限制回溯范围和输出长度，再按 CP949 恢复磁盘文件名；22 个输出均为有效 BMP，独立 Python 解码结果一致。

**F-003（已验证，E-003/E-005）：查看器需要兼容原始字节标识和 Unicode 文件名。** MDL/NAR 的内部名称继续保留原始字节；资源查找增加可逆的 CP949/Unicode 别名，只有转换无错误且能往返时才使用。先查原名，既有路径优先级不变；不改 MDL、不把非法字节静默替换为问号。同时拒绝将目录当成资源文件。

**F-004（已验证，E-005）：选择 Bin 时应发现同级 Data。** 补充游戏目录为 `Bin` 或 `HL` 时，资源查找加入上一级安装目录、Data 及已解包挂载目录。模型自己的松散资源仍优先。

## 读取与验证路径

P-001（素材整理）：先对原 common 建基线（E-001/F-001），解析 NAR 原始索引并按条目时间选候选（E-001/F-002），核 CRC 后缺失项原子写入；已解包 locale PAK 只覆盖本轮导入且 hash 仍相同的旧 NAR（E-002/F-001）。最终重新读取全部 common 文件、六包与 locale 源树（E-002）。

P-002（查看器）：原始 MDL 名称 → 原名/可逆韩文别名 → 模型目录优先 → 松散文件或 NAR 内容读取（E-003/E-005，F-003/F-004）→ 实际 GL 预览 → 关联导出 → 清空游戏目录配置后重开导出树（E-006/E-007）。

可在已构建候选中运行 `ctest --test-dir build/hlam-zh-csol-resources --output-on-failure` 重现格式检查。实际素材读取测试由 `CSOLFormatsTests --resources <资源目录> <预期清单.json> <结果.json>` 执行；`--nar` 强制 NAR 来源。预期清单是本机独立解码/全量整理结果，不在公开仓库分发。

## 画面、导出与部署结果

`v_leapstrikegunex` 实际 GL 验证 360 组姿态、组合动作与离线导出重开；导出 183 文件、67,080,645 字节，缺失清单为空。`v_soulreaper` 验证 Draw 模型事件与 JSON 动画；导出 197 文件、95,584,893 字节，缺失清单为空。两份导出的全部资源都在完整 common 内解析到，不再回退 NAR；每个文件均独立比较源文件、清单与导出 SHA-256。

最终包再解压运行，17 张包对照图中 16 张逐像素相同；连续播放的 `parts-loop.png` 有 6/614,400 像素差异，最大单通道差 1。此差异如实保留；确定帧、离线图、主体单独绘制和骨骼负对照均逐像素一致。未把此项标为全部画面位级相同。

安装包 40 文件逐项校验，实际更新 4 个变动文件。安装前确认用户 HLAM 未运行，备份被替换文件及 INI；部署后 40 文件匹配候选、INI hash 不变。累计补丁以固定上游 `4df74a58a50438b8f4b974e04ab9f24fdfcbb811` 回放，根 Git index 未变。

## 下一步

新全库发现 10 个 `_parts` 命名候选，骨骼/动作清单保留在 `completed-common-parts-candidates.json`。目前只对既有两组 leapstrikegun 配对启用自动组合。alterationammogun 的 parts01/02、linkattacksg、roboarm、sfboomerang 系列仍须追踪真实客户端连接、动作和生命周期；不能把文件名或共享骨骼当成游戏行为证据。

完整 CSO 目标保持 active：其余拆分关系、fadeTex/bindTex/colorTex、SPR 分组帧、v20/v21、世界特效、角色编号映射及 CSO 同相机/时间/姿态对照继续开放。
