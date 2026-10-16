package dev.goldcraft.world;

import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.damage.DamageType;
import net.minecraft.entity.damage.DamageTypes;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.tag.TagKey;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.util.Identifier;

/** Positive opt-in, never infer an active hit merely from an attributed player. */
public final class ActivePlayerDamage {
    private static final TagKey<DamageType> ACTIVE=TagKey.of(RegistryKeys.DAMAGE_TYPE,Identifier.of("goldcraft","active_player_attack"));
    private ActivePlayerDamage(){}
    static boolean eligible(DamageSource source,int kind){
        if(!(source.getAttacker() instanceof ServerPlayerEntity)||kind==3)return false;
        // Periodic/instant potion execution is dispatched before this classifier.
        // Magic, thorns, TNT, fire and other owner-attributed damage stay unscaled.
        return kind==4||source.isOf(DamageTypes.PLAYER_ATTACK)||source.isOf(DamageTypes.ARROW)
            ||source.isOf(DamageTypes.TRIDENT)||source.isIn(ACTIVE);
    }
}
