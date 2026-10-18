package dev.goldcraft.mixin;

import dev.goldcraft.world.HostExplosions;
import dev.goldcraft.world.NativeTntDamage;
import dev.goldcraft.world.NativeImpulse;
import net.minecraft.util.math.Vec3d;
import net.minecraft.entity.Entity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.block.BlockState;
import net.minecraft.fluid.FluidState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.BlockView;
import net.minecraft.world.World;
import net.minecraft.world.explosion.Explosion;
import net.minecraft.world.explosion.ExplosionBehavior;
import org.spongepowered.asm.mixin.*;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import com.llamalad7.mixinextras.injector.wrapoperation.*;
import java.util.Optional;

@Mixin(Explosion.class)
public abstract class HostExplosionMixin {
    @Shadow @Final private World world;
    @Unique private HostExplosions.Context goldcraft$host;
    @Inject(method="collectBlocksAndDamageEntities",at=@At("HEAD"),require=1)
    private void goldcraft$begin(CallbackInfo ci) {
        goldcraft$host=HostExplosions.begin(world,(Explosion)(Object)this);
    }
    @WrapOperation(method="collectBlocksAndDamageEntities",at=@At(value="INVOKE",target="Lnet/minecraft/entity/Entity;damage(Lnet/minecraft/entity/damage/DamageSource;F)Z"),require=1,allow=1)
    private boolean goldcraft$hurt(Entity target,DamageSource source,float amount,Operation<Boolean> original) {
        return NativeTntDamage.hurt(world,(Explosion)(Object)this,target,source,()->original.call(target,source,amount));
    }
    @WrapOperation(method="collectBlocksAndDamageEntities",at=@At(value="INVOKE",target="Lnet/minecraft/entity/Entity;setVelocity(Lnet/minecraft/util/math/Vec3d;)V"),require=1,allow=1)
    private void goldcraft$impulse(Entity target,Vec3d velocity,Operation<Void> original) {
        // This point follows exposure, resistance and NeoForge's final knockback event.
        // Damage-free wind bursts use it too; hurt return values do not gate the impulse.
        Vec3d delta=velocity.subtract(target.getVelocity());
        original.call(target,velocity);
        NativeImpulse.explosion(target,delta);
    }
    @WrapOperation(method="collectBlocksAndDamageEntities",at=@At(value="INVOKE",target="Lnet/minecraft/world/explosion/ExplosionBehavior;getBlastResistance(Lnet/minecraft/world/explosion/Explosion;Lnet/minecraft/world/BlockView;Lnet/minecraft/util/math/BlockPos;Lnet/minecraft/block/BlockState;Lnet/minecraft/fluid/FluidState;)Ljava/util/Optional;"),require=1,allow=1)
    private Optional<Float> goldcraft$resistance(ExplosionBehavior behavior,Explosion explosion,BlockView view,BlockPos pos,BlockState state,FluidState fluid,Operation<Optional<Float>> original) {
        Optional<Float> vanilla=original.call(behavior,explosion,view,pos,state,fluid);
        if(goldcraft$host==null)return vanilla;
        BlockState host=goldcraft$host.state(pos);
        if(host.isAir())return vanilla;
        Optional<Float> mapped=original.call(behavior,explosion,view,pos,host,fluid);
        return vanilla.isEmpty()?mapped:mapped.isEmpty()?vanilla:Optional.of(Math.max(vanilla.get(),mapped.get()));
    }
    @WrapOperation(method="collectBlocksAndDamageEntities",at=@At(value="INVOKE",target="Lnet/minecraft/world/explosion/ExplosionBehavior;canDestroyBlock(Lnet/minecraft/world/explosion/Explosion;Lnet/minecraft/world/BlockView;Lnet/minecraft/util/math/BlockPos;Lnet/minecraft/block/BlockState;F)Z"),require=1,allow=1)
    private boolean goldcraft$destroy(ExplosionBehavior behavior,Explosion explosion,BlockView view,BlockPos pos,BlockState state,float power,Operation<Boolean> original) {
        boolean vanilla=original.call(behavior,explosion,view,pos,state,power);
        if(!vanilla||goldcraft$host==null)return vanilla;
        BlockState host=goldcraft$host.state(pos);
        if(host.isAir())return true;
        boolean permitted=original.call(behavior,explosion,view,pos,host,power);
        if(permitted)goldcraft$host.reached(pos);
        return permitted;
    }
    // Detonate listeners have all returned. Honor the final list, including
    // cancellation/KEEP and edits by another Mod; never send at event priority.
    @Inject(method="affectWorld",at=@At("HEAD"),require=1)
    private void goldcraft$commit(boolean particles,CallbackInfo ci) {
        if(goldcraft$host!=null)goldcraft$host.commit((Explosion)(Object)this);
    }
}
