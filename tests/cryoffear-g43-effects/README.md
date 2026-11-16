# G43 实际效果验证

共用tests/cryoffear-p345-effect-dispatch/run.py，传入--g43-proof、--g43-aurora-proof、--sample和--client-sample才运行G43场景。修改前证据和两个原样本SHA/实际字节区均由运行器核验；不传G43证据仍可验证旧七枪。

实际G43类执行Deploy、机瞄、发射与Holster；生产GlockEffects、消息编码器及native事件/控制发送器真实运行。覆盖原26、两条相机消息顺序、附件1版本包、ShotState6、真实散布快照、序列边界与缺writer后的恢复。原生音频/latch/目标弹道是标注的受控端点，不代表整枪网络或游戏验收。

完整来源、验证层与后续边界见[阶段报告](../../docs/2026-11-16_接入-G43实际效果与附件消息-report.md)。
