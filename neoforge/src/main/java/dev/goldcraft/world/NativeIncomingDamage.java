package dev.goldcraft.world;

import net.minecraft.entity.damage.DamageSource;
import net.minecraft.server.network.ServerPlayerEntity;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.common.damagesource.DamageContainer;
import net.neoforged.neoforge.event.entity.living.LivingIncomingDamageEvent;

/** One real, deduplicated native hit enters the normal mod cancellation stage. */
public final class NativeIncomingDamage {
    private NativeIncomingDamage(){}
    public static float accepted(ServerPlayerEntity player,DamageSource source,float amount){
        if(player.isInvulnerableTo(source))return 0;
        var event=new LivingIncomingDamageEvent(player,new DamageContainer(source,amount));
        NeoForge.EVENT_BUS.post(event);
        if(event.isCanceled())return 0;
        float accepted=event.getAmount();
        if(!Float.isFinite(accepted)||accepted<0)throw new IllegalArgumentException("Invalid incoming native damage");
        // MC may cancel/reduce the hit, but cannot enlarge ReHLDS's validated
        // request. Health, resistance and absorption remain native-owned.
        return Math.min(amount,accepted);
    }
}
