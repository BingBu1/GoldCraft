package dev.goldcraft.test;

import com.mojang.authlib.GameProfile;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.HostSession;
import dev.goldcraft.world.SharedVitals;
import io.netty.channel.embedded.EmbeddedChannel;
import net.neoforged.neoforge.gametest.GameTestHolder;
import net.neoforged.neoforge.gametest.PrefixGameTestTemplate;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Items;
import net.minecraft.network.ClientConnection;
import net.minecraft.network.NetworkSide;
import net.minecraft.network.packet.s2c.play.DeathMessageS2CPacket;
import net.minecraft.network.packet.s2c.play.PlayerRespawnS2CPacket;
import net.minecraft.server.network.ConnectedClientData;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.stat.Stats;
import net.minecraft.test.GameTest;
import net.minecraft.test.TestContext;
import net.minecraft.world.GameMode;
import java.util.UUID;

/** Real 1.21 player/packet lifecycle; host snapshots are the controlled test input. */
@GameTestHolder("goldcraft_tests")
@PrefixGameTestTemplate(false)
public final class SharedVitalsTest {
    private static final class Fixture implements AutoCloseable {
        final NavigationTest.Fixture map;
        final UUID uuid=UUID.randomUUID();
        final EmbeddedChannel channel;
        long tick;
        Fixture(TestContext context) {
            SharedVitals.clear();HostSession.stopped();
            map=NavigationTest.fixture(context,"nav_fixture");
            var data=ConnectedClientData.createDefault(new GameProfile(uuid,"VitalsTest"),false);
            var player=new ServerPlayerEntity(map.world().getServer(),map.world(),data.gameProfile(),data.syncedOptions());
            // This is Minecraft TestContext's own embedded connection pattern,
            // using an ordinary survival player instead of its creative subclass.
            var connection=new ClientConnection(NetworkSide.SERVERBOUND);
            channel=new EmbeddedChannel(connection);
            map.world().getServer().getPlayerManager().onPlayerConnect(connection,player,data);
            player.changeGameMode(GameMode.SURVIVAL);
            snapshot(true,1,100,0);
            HostSession.paired(uuid,map.host().epoch());
            HostSession.tick(map.world().getServer(),map.host());
            player.changeGameMode(GameMode.SURVIVAL);
            player.onTeleportationDone();
            SharedVitals.active(player,true);
            channel.outboundMessages().clear();
        }
        ServerPlayerEntity player(){return map.world().getServer().getPlayerManager().getPlayer(uuid);}
        HostWorldState.Actor snapshot(boolean alive,int spawn,float health,long ack) {
            int flags=16|64|(alive?1:0);
            var w=new Wire.Writer().i64(map.host().epoch()).i64(++tick).f32(tick/20f).i32(0).i32(1)
                .i32(1).i32(4).i32(7).i32(2).i32(flags).i32(spawn).f32(health).f32(0)
                .f32(4.5f*32).f32(-12.5f*32).f32(44).f32(0).f32(0).f32(0).f32(0).f32(0)
                .f32(-16).f32(-16).f32(-36).f32(16).f32(16).f32(36)
                .bytes(Wire.uuid(uuid)).i32(spawn).i64(ack);
            map.host().actors(w.toByteArray());return map.host().actor(uuid);
        }
        void hostTick(){HostSession.tick(map.world().getServer(),map.host());}
        @Override public void close(){
            var player=player();
            if(player!=null){HostSession.disconnect(player);map.world().getServer().getPlayerManager().remove(player);}
            SharedVitals.clear();HostSession.stopped();channel.finishAndReleaseAll();
        }
    }
    private static int deaths(ServerPlayerEntity player){
        return player.getStatHandler().getStat(Stats.CUSTOM.getOrCreateStat(Stats.DEATHS));
    }
    private static long packets(Fixture f,Class<?> packet){
        return f.channel.outboundMessages().stream().filter(packet::isInstance).count();
    }

    @GameTest(templateName="empty",batchId="vitals_fast_respawn",tickLimit=80)
    public void nativeRespawnBeforeMcRemovalReplacesPlayerExactlyOnce(TestContext context){
        try(var f=new Fixture(context)){
            var old=f.player();old.getInventory().setStack(0,new ItemStack(Items.DIAMOND,3));
            var dead=f.snapshot(false,1,1,0);SharedVitals.prepare(old,f.map.host(),dead);
            context.assertTrue(!old.isAlive()&&!old.isRemoved()&&old.deathTime<20,"Fixture must be before MC death removal");
            context.assertTrue(deaths(old)==1,"Native death flag with health1 must confirm exactly one MC death");
            f.hostTick();f.hostTick();
            context.assertTrue(deaths(old)==1,"Repeated native death snapshots duplicated death statistics");
            f.channel.outboundMessages().clear();
            f.snapshot(true,2,100,0);f.hostTick();
            var replacement=f.player();
            context.assertTrue(replacement!=old,"Fast native respawn resurrected the old MC entity instead of PlayerManager respawn");
            context.assertTrue(replacement.isAlive()&&!replacement.isRemoved()&&replacement.getHealth()==20,"Replacement is not alive with native health");
            context.assertTrue(old.isRemoved()&&replacement.networkHandler.player==replacement,"Respawn left old entity or stale network handler");
            context.assertTrue(f.channel.outboundMessages().stream().anyMatch(PlayerRespawnS2CPacket.class::isInstance),"Actual client respawn packet missing");
            f.hostTick();
            context.assertTrue(f.player()==replacement&&deaths(replacement)==1,"Stable native spawn repeated respawn/death");
        }
        context.complete();
    }

    @GameTest(templateName="empty",batchId="vitals_rejected_death",tickLimit=80)
    public void rejectedLethalDamageKeepsInventoryAndNeverRespawns(TestContext context){
        try(var f=new Fixture(context)){
            var player=f.player();player.getInventory().setStack(0,new ItemStack(Items.DIAMOND,3));
            context.assertTrue(player.damage(player.getDamageSources().genericKill(),1000),"Vanilla lethal damage did not reach the survival player");
            SharedVitals.capture(f.map.world().getServer(),f.map.host());
            context.assertTrue(SharedVitals.pending(f.uuid)==1,"Lethal damage did not produce a pending health delta");
            context.assertTrue(SharedVitals.prepare(player,f.map.host(),f.map.host().actor(f.uuid)),"Unacknowledged lethal damage did not await native authority");
            SharedVitals.active(player,false);
            for(int i=0;i<25;i++)player.baseTick();
            context.assertTrue(!player.isRemoved()&&player.deathTime==0,"Unconfirmed death removed the player before the authority replied");
            context.assertTrue(deaths(player)==0&&player.getInventory().getStack(0).getCount()==3,"Unconfirmed death changed inventory/statistics");
            context.assertTrue(packets(f,DeathMessageS2CPacket.class)==0,"Unconfirmed death reached the client death screen");
            // The native server consumed event 1 but rejected its health change.
            var alive=f.snapshot(true,1,100,1);SharedVitals.prepare(player,f.map.host(),alive);f.hostTick();
            context.assertTrue(f.player()==player&&player.isAlive()&&player.getHealth()==20,"Rejected lethal damage failed to restore the original living player");
            context.assertTrue(SharedVitals.pending(f.uuid)==0&&!SharedVitals.deferRemoval(player),"Rejected lethal delta did not leave the ledger");
            context.assertTrue(deaths(player)==0&&player.getInventory().getStack(0).getCount()==3,"Rejected lethal damage lost items or counted a death");
            context.assertTrue(packets(f,PlayerRespawnS2CPacket.class)==0,"Rejected damage incorrectly triggered a new player lifecycle");
        }
        context.complete();
    }

    @GameTest(templateName="empty",batchId="vitals_confirmed_death",tickLimit=80)
    public void confirmedLethalDamageDropsAndCountsOnce(TestContext context){
        try(var f=new Fixture(context)){
            var player=f.player();player.getInventory().setStack(0,new ItemStack(Items.DIAMOND,3));
            context.assertTrue(player.damage(player.getDamageSources().genericKill(),1000),"Vanilla lethal damage rejected");
            SharedVitals.capture(f.map.world().getServer(),f.map.host());
            var dead=f.snapshot(false,1,1,1);
            SharedVitals.prepare(player,f.map.host(),dead);
            SharedVitals.prepare(player,f.map.host(),dead);
            context.assertTrue(deaths(player)==1&&player.getInventory().getStack(0).isEmpty(),"Confirmed death must drop inventory and count once");
            context.assertTrue(packets(f,DeathMessageS2CPacket.class)==1,"Confirmation sent zero or duplicate death messages");
            context.assertTrue(!SharedVitals.deferRemoval(player)&&SharedVitals.pending(f.uuid)==0,"Confirmed death still waits for authority");
            for(int i=0;i<21;i++)player.baseTick();
            context.assertTrue(player.isRemoved(),"Confirmed MC corpse did not reach vanilla removal");
            f.channel.outboundMessages().clear();
            f.snapshot(true,2,75,1);f.hostTick();
            var replacement=f.player();
            context.assertTrue(replacement!=player&&replacement.isAlive()&&replacement.getHealth()==15,"Late native respawn did not replace the removed player with authoritative health");
            context.assertTrue(replacement.networkHandler.player==replacement&&deaths(replacement)==1,"Late respawn retained stale handler or duplicated death statistics");
            context.assertTrue(packets(f,PlayerRespawnS2CPacket.class)>0,"Late respawn did not notify the client");
        }
        context.complete();
    }
}
