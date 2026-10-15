package dev.goldcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import dev.goldcraft.world.NativeTntDamage;
import dev.goldcraft.world.NativeTntOrigin;
import dev.goldcraft.world.TntOwnerAccess;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.TntEntity;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(TntEntity.class)
public abstract class TntOwnerMixin implements TntOwnerAccess {
    @Unique private NativeTntOrigin goldcraft$owner;
    @Override public NativeTntOrigin goldcraft$owner() { return goldcraft$owner; }

    @Inject(method="<init>(Lnet/minecraft/world/World;DDDLnet/minecraft/entity/LivingEntity;)V",at=@At("RETURN"),require=1)
    private void goldcraft$capture(World world,double x,double y,double z,LivingEntity igniter,CallbackInfo ci) {
        goldcraft$owner=NativeTntDamage.capture((TntEntity)(Object)this,igniter);
    }

    @WrapMethod(method="explode")
    private void goldcraft$explode(Operation<Void> original) {
        NativeTntDamage.explode((TntEntity)(Object)this,()->original.call());
    }

    @Inject(method="copyFrom",at=@At("RETURN"),require=1)
    private void goldcraft$copy(Entity original,CallbackInfo ci) {
        goldcraft$owner=original instanceof TntOwnerAccess owner?owner.goldcraft$owner():null;
    }

    @Inject(method="writeCustomDataToNbt",at=@At("RETURN"),require=1)
    private void goldcraft$write(NbtCompound nbt,CallbackInfo ci) {
        if(goldcraft$owner==null)return;
        var value=new NbtCompound();
        value.putLong("epoch",goldcraft$owner.epoch());value.putInt("slot",goldcraft$owner.slot());
        value.putInt("serial",goldcraft$owner.serial());value.putInt("spawn",goldcraft$owner.spawn());
        if(goldcraft$owner.pairedPlayer()!=null)value.putUuid("paired",goldcraft$owner.pairedPlayer());
        nbt.put("goldcraft_tnt_owner",value);
    }

    @Inject(method="readCustomDataFromNbt",at=@At("RETURN"),require=1)
    private void goldcraft$read(NbtCompound nbt,CallbackInfo ci) {
        var value=nbt.getCompound("goldcraft_tnt_owner");
        long epoch=value.getLong("epoch");int slot=value.getInt("slot"),serial=value.getInt("serial"),spawn=value.getInt("spawn");
        goldcraft$owner=epoch==0||slot<1||slot>64||serial==0||spawn==0?null:
            new NativeTntOrigin(epoch,slot,serial,spawn,value.containsUuid("paired")?value.getUuid("paired"):null);
    }
}
