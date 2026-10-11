package dev.goldcraft.client;

import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.world.NativePlayerHitboxEntity;
import net.minecraft.client.MinecraftClient;

/** Epic may sense a native body only while its tracked identity matches server authority. */
public final class HostEpicTargets {
    private HostEpicTargets(){}
    public static boolean canTarget(NativePlayerHitboxEntity target) {
        var client=MinecraftClient.getInstance();
        if(!HostInput.hosted()||!HostInput.controlling()||client.player==null
            ||target.getWorld()!=client.world||target.isRemoved()||!target.isAlive()||!target.canHit())return false;
        var host=GoldCraftClient.HOST;
        return canTarget(host,host.actor(client.player.getUuid()),target.hostEpoch(),target.hostSlot(),target.hostSerial(),target.hostLife());
    }
    static boolean canTarget(HostWorldState host,HostWorldState.Actor viewer,long epoch,int slot,int serial,int life) {
        if(epoch==0||epoch!=host.epoch()||!alive(viewer)||!viewer.minecraftForm()||(viewer.flags()&32)==0||viewer.slot()==slot)return false;
        for(var actor:host.actors())if(actor.slot()==slot)
            return actor.serial()==serial&&actor.life()==life&&alive(actor)&&viewer.team()!=actor.team()
                &&!(actor.minecraftForm()&&(actor.flags()&32)!=0);
        return false;
    }
    private static boolean alive(HostWorldState.Actor actor) {
        return actor!=null&&actor.life()!=0&&(actor.flags()&5)==1&&actor.health()>0&&(actor.team()==1||actor.team()==2);
    }
}
