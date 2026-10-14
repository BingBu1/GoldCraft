package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.GameplayPolicy;
import dev.goldcraft.bridge.HostWorldState;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;

public final class HostGameplayRules {
    private static GameplayPolicy policy=GameplayPolicy.defaults();
    private HostGameplayRules(){}
    public static void reset(){policy=GameplayPolicy.defaults();}
    public static void accept(HostWorldState host,byte[] payload){
        var next=GameplayPolicy.decode(payload);
        if(next.supersedes(policy,host.epoch())){
            policy=next;
            GoldCraft.LOGGER.info("Gameplay rules: world={} revision={} clearRevision={} clearBlocks={} deathDrops={}",
                Long.toUnsignedString(next.world()),Long.toUnsignedString(next.revision()),
                Long.toUnsignedString(next.clearRevision()),next.clearBlocks(),next.dropItems());
        }
    }
    public static void tick(MinecraftServer server,HostWorldState host){
        if(!GoldCraft.gameplayRulesAvailable()||policy.world()!=host.epoch())return;
        var world=HostSession.hostWorld(server,host);
        if(world!=null)HostWorldReset.clearRoundBlocks(world,policy.world(),policy.clearRevision());
    }
    public static boolean suppressPlayerDrops(ServerPlayerEntity player){
        return GoldCraft.gameplayRulesAvailable()&&!policy.dropItems()&&policy.world()!=0&&
            HostWorldReset.active(player.getServerWorld(),policy.world());
    }
}
