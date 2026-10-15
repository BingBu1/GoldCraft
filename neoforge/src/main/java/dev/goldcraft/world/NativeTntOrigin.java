package dev.goldcraft.world;

import dev.goldcraft.bridge.HostWorldState;
import java.util.UUID;

/** Ignition-time native identity; form changes may advance life but not spawn. */
public record NativeTntOrigin(long epoch,int slot,int serial,int spawn,UUID pairedPlayer) {
    public HostWorldState.Actor resolve(HostWorldState host) {
        if(epoch==0||epoch!=host.epoch())return null;
        for(var actor:host.actors()) {
            if(actor.slot()!=slot||actor.serial()!=serial||actor.spawn()!=spawn)continue;
            if(pairedPlayer!=null&&((actor.flags()&16)==0||!pairedPlayer.equals(actor.minecraftPlayer())))return null;
            return actor;
        }
        return null;
    }
}
