package dev.goldcraft.mixin;

import dev.goldcraft.world.NativeHealthAttribute;
import net.minecraft.entity.attribute.EntityAttribute;
import net.minecraft.entity.attribute.EntityAttributeInstance;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.ModifyArg;
import org.spongepowered.asm.mixin.injection.Redirect;

@Mixin(EntityAttributeInstance.class)
abstract class NativeHealthAttributeMixin implements NativeHealthAttribute {
    @Shadow private double baseValue;
    @Shadow protected abstract void onUpdate();
    @Unique private boolean goldcraft$nativeHealth;
    @Unique private double goldcraft$originalBase;
    @Override public void goldcraft$healthOwnership(boolean owned,boolean restoreBase){
        if(owned==goldcraft$nativeHealth)return;
        if(owned)goldcraft$originalBase=baseValue;
        else if(restoreBase)baseValue=goldcraft$originalBase;
        goldcraft$nativeHealth=owned;onUpdate();
    }
    @Override public boolean goldcraft$healthOwned(){return goldcraft$nativeHealth;}
    @Override public double goldcraft$originalHealthBase(){return goldcraft$nativeHealth?goldcraft$originalBase:baseValue;}
    @Redirect(method="computeValue",at=@At(value="INVOKE",target="Lnet/minecraft/entity/attribute/EntityAttribute;clamp(D)D"))
    private double goldcraft$nativeRange(EntityAttribute attribute,double value){
        return goldcraft$nativeHealth&&Double.isFinite(value)?Math.clamp(value,.2,2_000_000):attribute.clamp(value);
    }
    @ModifyArg(method="toNbt",at=@At(value="INVOKE",target="Lnet/minecraft/nbt/NbtCompound;putDouble(Ljava/lang/String;D)V"),index=1)
    private double goldcraft$persistOriginalBase(double value){return goldcraft$nativeHealth?goldcraft$originalBase:value;}
}
