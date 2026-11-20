# CoF FAMAS G2 第三人称模型

CoF 地面模型的两组枪身已挂到 CS 原生 FAMAS 的右手子骨。模型在三个沙盒角色上完成了离线握姿校准，保留原世界模型的大小、两张 512×512 贴图和全部 3,451 个三角面。它可由本地原始资源重建。FAMAS 的游戏内购买、人物后端与第三人称实机画面仍待接入验证。

## 来源和制作

CoF 的 x86 游戏模块基址为 `0x10000000`，SHA-256 为 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。修改前将两条 Deploy 分支所在函数（VA `0x1003AC00`）、Precache（`0x1003BB40`）和公共 Deploy（`0x1011C1A0`）共 1,313 字节与 IDA 逐一核同。两条掏枪分支都把 `models/null.mdl` 和 `onehanded` 传给公共 Deploy。CSOL x86 客户端的同名骨合并及未匹配子骨拼接位于 VA `0x02ACCA80`，已按归档索引核同模块和该函数的 1,163 字节。私有证据保存在 `analysis/cof-weapons-20261111/famas-20261118/player-model`。

新的 P 模型属于用户要求的宿主制作。作者工具用 CoF 原 `w_famas.mdl` 的枪管完整端环、对侧宽度和握把四个位置建立局部坐标，再对齐 CS 原 `p_famas.mdl` 的相应顶点。刚体变换保留原比例，两个几何组的 953 和 956 个顶点全部跟随 `Bip01 R Hand` 的直接子骨 `p_famas`。两组分别占用一个 Studio bodypart，可同时显示。

CoF 原 W 没有附件，原 V 的附件由其 `fam_muzzle` 骨驱动。CS 原 P 有一个独立附件。新 P 的第一个附件取自 CoF 世界枪身枪口的 12 个连续端面顶点，供宿主的第三人称枪口效果使用。完整附件表在引擎保留的 81,784 字节模型前缀内。

## 几何、握姿和验证

| 验证项 | 结果 |
| --- | --- |
| 几何与贴图 | 对照全部 10,353 个三角顶点角。整数 UV、贴图像素、调色板及皮肤表的两个材质索引均核同。最大位置误差 0.00000096 单位，最大法线误差 0.000000046。 |
| 枪口 | 12 条轮廓边逐条检查。新附件到变换后源端环中心相距 0.00000091 单位。 |
| 现用 `rifle` 动作 | `urban`、`csol95`、`csol168` 均测试站立与蹲姿。在 10 个持枪、30 个开火样本中，左右手网格均与枪身相交。30 个换弹样本右手始终接触，左手在 18 个样本接触，其余离枪时点与 CS 参照逐样本一致。 |
| 原生 `carbine` 动作对照 | 同样抽查 70 个姿态。两处差别均在 `urban` 换弹第 15 帧，CoF 模型较大的轮廓仍与左手相交。游戏类继续使用已有的 `rifle` 动作。 |

姿态检查使用实际动作的中性混合轨和每条轨道至多五个不同整帧。输出包含逐帧双向三角形交叉检测和同机位侧视叠图。多人网络插值、实机肩托位置、推搡动作与第三人称特效继续由实际客户端验收。

现有 12 组模型回归和本次 3 组原始资源回归通过，另用 CLI 独立生成两次，607,608 字节模型逐字节相同。SHA-256 为 `a113f1531c331790ce9e0860013f57ba9f9cfe3d46e025138f1c83a840342635`。一次带 cProfile 的完整离线制作耗时 2.00 秒，Studio 解析器占 0.735 秒累计时间。游戏循环直接读取产物。验证记录在 `build/cof-famas-player-20261120-01` 的 `receipt.json`、`geometry-verified.json` 和 `pose-comparison.json`。

从项目根目录运行以下命令，传入本地原版模型与一个新的工作区输出目录。

```bash
python -B tools/Author-CryOfFearFamasPlayer.py \
  --world-model "$COF_GAME/models/weapons/famas/w_famas.mdl" \
  --cs-player-model sandbox/cs-server/Half-Life/cstrike/models/p_famas.mdl \
  --output build/cof-famas-player
```
