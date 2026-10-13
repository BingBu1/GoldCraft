package dev.goldcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.goldcraft.world.NativeStatusExecution;
import dev.goldcraft.world.NativeStatusInstance;
import dev.goldcraft.world.NativeStatusOrigin;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.effect.StatusEffectInstance;
import net.minecraft.entity.effect.StatusEffect;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(StatusEffectInstance.class)
abstract class NativeStatusInstanceMixin implements NativeStatusInstance {
    @Unique private NativeStatusOrigin goldcraft$origin;
    @Unique private boolean goldcraft$replaceOrigin;
    public NativeStatusOrigin goldcraft$statusOrigin(){return goldcraft$origin;}
    public void goldcraft$statusOrigin(NativeStatusOrigin origin){goldcraft$origin=origin;}
    @Inject(method="copyFrom",at=@At("RETURN"))
    private void goldcraft$copyOrigin(StatusEffectInstance source,CallbackInfo ci){
        goldcraft$origin=((NativeStatusInstance)(Object)source).goldcraft$statusOrigin();
    }
    @Inject(method="upgrade",at=@At("HEAD"))
    private void goldcraft$beforeUpgrade(StatusEffectInstance incoming,CallbackInfoReturnable<Boolean> cir){
        var self=(StatusEffectInstance)(Object)this;
        boolean stronger=incoming.getAmplifier()>self.getAmplifier();
        boolean longer=incoming.getAmplifier()==self.getAmplifier()&&!self.isInfinite()
            &&(incoming.isInfinite()||incoming.getDuration()>self.getDuration());
        goldcraft$replaceOrigin=stronger||longer;
    }
    @Inject(method="upgrade",at=@At("RETURN"))
    private void goldcraft$afterUpgrade(StatusEffectInstance incoming,CallbackInfoReturnable<Boolean> cir){
        // Vanilla has now saved the old instance/hidden stack with its own origin.
        if(goldcraft$replaceOrigin)goldcraft$origin=((NativeStatusInstance)(Object)incoming).goldcraft$statusOrigin();
        goldcraft$replaceOrigin=false;
    }
    @WrapOperation(method="update",at=@At(value="INVOKE",
        target="Lnet/minecraft/entity/effect/StatusEffect;applyUpdateEffect(Lnet/minecraft/entity/LivingEntity;I)Z"))
    private boolean goldcraft$effectTick(StatusEffect effect,LivingEntity entity,int amplifier,Operation<Boolean> original){
        try(var scope=NativeStatusExecution.effect(entity,(StatusEffectInstance)(Object)this,true)){
            return original.call(effect,entity,amplifier);
        }
    }
    @WrapOperation(method="onApplied",at=@At(value="INVOKE",
        target="Lnet/minecraft/entity/effect/StatusEffect;onApplied(Lnet/minecraft/entity/LivingEntity;I)V"))
    private void goldcraft$effectApplied(StatusEffect effect,LivingEntity entity,int amplifier,Operation<Void> original){
        try(var scope=NativeStatusExecution.effect(entity,(StatusEffectInstance)(Object)this,false)){original.call(effect,entity,amplifier);}
    }
}
