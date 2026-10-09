package dev.goldcraft.client;

import dev.goldcraft.net.MiningPayload;
import dev.goldcraft.world.HostRaycast;
import net.minecraft.client.MinecraftClient;
import net.minecraft.util.Hand;
import net.neoforged.neoforge.network.PacketDistributor;

/** Uses the existing CS attack mapping without changing native key bindings. */
public final class HostMiningInput {
    private static boolean sentHeld;
    private static long lastSent,epoch,revision;
    private static int serial,life;
    private HostMiningInput(){}
    public static void clear(){sentHeld=false;lastSent=epoch=revision=0;serial=life=0;}
    public static void tick(MinecraftClient client){
        var actor=client.player==null?null:GoldCraftClient.HOST.actor(client.player.getUuid());
        if(actor==null||client.getNetworkHandler()==null||!client.getNetworkHandler().hasChannel(MiningPayload.ID)){clear();return;}
        var policy=GoldCraftClient.HOST.mining();
        boolean held=HostInput.attacking()&&client.currentScreen==null&&!GoldCraftClient.HOST.freeze()&&!client.player.isUsingItem()
            &&client.crosshairTarget instanceof HostRaycast.Hit hit&&policy.canRequest(hit.model);
        long now=System.nanoTime();
        if(held!=sentHeld||epoch!=policy.epoch()||revision!=policy.revision()||serial!=actor.serial()||life!=actor.life()||held&&now-lastSent>=100_000_000L){
            PacketDistributor.sendToServer(new MiningPayload(policy.epoch(),policy.revision(),actor.serial(),actor.life(),held));
            sentHeld=held;lastSent=now;epoch=policy.epoch();revision=policy.revision();serial=actor.serial();life=actor.life();
        }
        if(held&&!client.player.handSwinging)client.player.swingHand(Hand.MAIN_HAND);
    }
}
