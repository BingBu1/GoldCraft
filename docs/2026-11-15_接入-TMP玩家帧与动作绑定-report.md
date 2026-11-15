# TMP 玩家帧与动作绑定

新增独立TMPOwnerView，复用已核收的canonical原生玩家输入/移动/生命状态。实际SDK与三个原人物模型检查通过；实际地图运行时/Host和客户端仍未接入TMP，本阶段不开放购买或部署。

## Evidence → Finding → Path

匹配原CoF MP为x86/VA基址10000000，SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63。原client身份 d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00 的共同输入/表现边界保持。本轮复用已核同的TMP Deploy1007E130（原首拔项14）、Post1007E8A0→共同1011D560和实际输入消费者，原库存规则备弹300或clip×3=90；没有拿Glock的首拔/伤害/容量替代TMP。

修改前记录：analysis/cof-weapons-20261111/tmp-integration-20261115/owner-before.json；完整原生类依据见tmp-integration-20261114/native-class-before.json；样本和冷资源原681字节核同见本日TMP冷资源报告。既有canonical运行时和VP70分离状态实现也在修改前读取，保留原有权威和回调退役契约。

| Evidence | Finding | Path |
| --- | --- | --- |
| 原TMP首拔项与已核共有帧输入 | 只借用共同输入/移动字段，独立保存TMP首拔与武器规则 | native/csol/server/weapons/guest_cof_tmp_owner.cpp/.hpp |
| 原武器规则90/300和实际服务端显式规则 | 无规则/坏规则拒绝，不从CS字段或客户端猜值 | 同上Prepare/Read |
| 用户要求合理CS/CSO动作与实际三模型标签 | 宿主明确选mp5上半身，Stock knife/CSO nataknife推击；站蹲标签都须真实存在 | 既有RecommendedLongGunAnimationPlan(LongGun::TMP,rig)及实际PlayerAnimationAdapter |
| 当前SDK owner/edict/model/spawn与canonical代际 | 旧生命/角色/地图/玩家被替代后旧句柄失效，输出保持 | 同上Current/Read/Invalidate |

动作选择是明确的宿主适配，不冒称原CoF第三人称骨骼/几何。原生TMP Deploy的onehanded扩展仍保持，最终CS/CSO握姿/接触和画面必须实测。未新增随机seed声明或手动偏移读取。

## 实现与验证

typed owner从canonical读取权威activity/pending、条件、原输入按下/松开、随机种子、原flags和来源模式，TMP的toggleAim、伤害、reserve规则及firstDraw独立。准备期间使用实际GET_MODEL_PTR关联模型和现有动画代际，仍保留回调重入/退役检查、代际耗尽拒绝以及错误输出保护。

build/cof-tmp-native-owner-20261115-01/receipt.json：x86实际SDK编译与10102检查零失败，前后输入稳定。包含10000次缓存读取不额外读取模型、独立首拔、正确输入/跳跃状态、90/300规则、重绑/旧句柄、重生、canonical替换、两个合成SDK玩家隔离，以及实际男女CSO模型六种动作的站蹲标签。组件不模拟完整游戏进程，两个SDK对象不等于真实多人验收。

新Read/Current只用固定成员和栈帧，借用已有canonical缓存；无新资源查找、同步I/O或临时堆容器。保留C++20、O3/ThinLTO/AVX2、precise FP和禁融合。没有实机CPU/帧时或绘制/音频验收，不把10000缓存检查称性能通过。

本轮也保留前阶段文档更新后封存复验：build/cof-tmp-cold-resources-20261115-02 为3463检查全过；build/cof-tmp-resource-author-20261115-02 为351检查/27CLI全过。所有候选仍未部署，VP70当前用户可用版本保持。

## 继续工作与持久交接

下一步把TMP owner接入实际地图/Host和库存角色重绑，接自动声音、独立粒子清理和客户端id14资源/枪口/HUD/购买，完成P附件/握姿与CSO手模后完整DLL、下载和游戏验收。全部25身份/变体Goal保持active。

共享文档精确待应用：PROJECT_STATE／PROGRESS追加上述独立owner和10102检查及边界；GOAL维持完整CoF；TODO保留TMP整枪、多人与性能，不重复VP70已获用户确认的购买。领域docs/COF_GOAL.md同步，不发送非打包跨聊消息。
