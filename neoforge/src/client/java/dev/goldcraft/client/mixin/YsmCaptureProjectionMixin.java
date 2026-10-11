package dev.goldcraft.client.mixin;

import dev.goldcraft.client.YsmCaptureProjection;
import org.joml.Matrix4f;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Pseudo;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyArg;

/** Keep YSM 2.6.5's native visibility calculation in the host export coordinate space. */
@Pseudo
@Mixin(targets="com.elfmcys.yesstevemodel.OoooOoo0o0000O0o00o0O0Oo",remap=false)
abstract class YsmCaptureProjectionMixin {
    @ModifyArg(method="oOo0OO0O0o000OO0O000oo0o(Lcom/mojang/blaze3d/vertex/VertexConsumer;Lcom/mojang/blaze3d/vertex/PoseStack$Pose;Lcom/elfmcys/yesstevemodel/O0OOo0OoOoo0o0o0oO000o00;[F[FIIIIFFFF)V",
        at=@At(value="INVOKE",target="Lcom/elfmcys/yesstevemodel/OoooOoo0o0000O0o00o0O0Oo;oOo0OO0O0o000OO0O000oo0o(Lcom/mojang/blaze3d/vertex/VertexConsumer;Lcom/mojang/blaze3d/vertex/PoseStack$Pose;Lorg/joml/Matrix4f;ZLcom/elfmcys/yesstevemodel/O0OOo0OoOoo0o0o0oO000o00;[F[FIIIIFFFFZ)V"),index=2)
    private static Matrix4f goldcraft$worldProjection(Matrix4f original) {
        return YsmCaptureProjection.forNativeDraw(original);
    }
}
