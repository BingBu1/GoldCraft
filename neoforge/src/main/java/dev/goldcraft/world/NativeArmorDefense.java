package dev.goldcraft.world;

import dev.goldcraft.mixin.NativeArmorAccessor;
import net.minecraft.enchantment.EnchantmentHelper;
import net.minecraft.entity.DamageUtil;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.registry.tag.DamageTypeTags;
import net.minecraft.server.network.ServerPlayerEntity;

/**
 * Equipment-only defense for an already accepted native hit. Native authority
 * applies resistance, then this enchantment factor, then the one absorption
 * pool. This helper never changes health or emits a second SharedVitals delta.
 * The native request/receipt integration must fence and deduplicate each call.
 */
public final class NativeArmorDefense {
    public record Reduction(float afterArmor,float enchantmentMultiplier) {
        public Reduction {
            if(!Float.isFinite(afterArmor)||afterArmor<0||!Float.isFinite(enchantmentMultiplier)
                ||enchantmentMultiplier<0||enchantmentMultiplier>1)
                throw new IllegalArgumentException("Invalid native armor reduction");
        }
    }
    private NativeArmorDefense(){}

    public static Reduction reduce(ServerPlayerEntity wearer,DamageSource source,float nativeDamage) {
        if(!Float.isFinite(nativeDamage)||nativeDamage<=0)
            throw new IllegalArgumentException("Positive finite native damage required");
        float amount=nativeDamage/MinecraftObjects.CS_HEALTH_PER_MC;
        // Creative immunity must not consume equipment durability. Calling the
        // protected stage directly otherwise skips PlayerEntity's outer guard.
        if(wearer.isCreative())return new Reduction(0,1);
        float afterArmor=((NativeArmorAccessor)wearer).goldcraft$applyNativeArmor(source,amount);
        float protection=source.isIn(DamageTypeTags.BYPASSES_EFFECTS)
            ||source.isIn(DamageTypeTags.BYPASSES_ENCHANTMENTS)?0:
            EnchantmentHelper.getProtectionAmount(wearer.getServerWorld(),wearer,source);
        return new Reduction(Math.min(nativeDamage,afterArmor*MinecraftObjects.CS_HEALTH_PER_MC),
            DamageUtil.getInflictedDamage(1,protection));
    }
}
