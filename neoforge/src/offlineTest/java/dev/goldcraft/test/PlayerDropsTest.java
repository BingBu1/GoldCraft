package dev.goldcraft.test;

import com.mojang.authlib.GameProfile;
import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.world.HostGameplayRules;
import dev.goldcraft.world.HostSession;
import dev.goldcraft.world.HostWorldReset;
import dev.goldcraft.world.SharedVitals;
import io.netty.channel.embedded.EmbeddedChannel;
import net.minecraft.entity.EntityType;
import net.minecraft.entity.EquipmentSlot;
import net.minecraft.entity.ExperienceOrbEntity;
import net.minecraft.entity.ItemEntity;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Items;
import net.minecraft.nbt.NbtCompound;
import net.minecraft.network.ClientConnection;
import net.minecraft.network.NetworkSide;
import net.minecraft.network.packet.c2s.play.PlayerActionC2SPacket;
import net.minecraft.server.network.ConnectedClientData;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.stat.Stats;
import net.minecraft.test.GameTest;
import net.minecraft.test.TestContext;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Direction;
import net.minecraft.world.GameMode;
import net.minecraft.world.GameRules;
import net.neoforged.bus.api.EventPriority;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.entity.item.ItemTossEvent;
import net.neoforged.neoforge.event.entity.living.LivingDropsEvent;
import net.neoforged.neoforge.gametest.GameTestHolder;
import net.neoforged.neoforge.gametest.PrefixGameTestTemplate;
import java.lang.reflect.Field;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import java.util.function.Consumer;

/** Real network players and vanilla death/drop events under a decoded host policy.
 * The loopback peer is a protocol fixture, not evidence of a running ReHLDS cvar.
 * Run synchronously: the fixture owns the otherwise unconfigured global authority
 * only during each test method, and releases it before the next server tick.
 */
@GameTestHolder("goldcraft_tests")
@PrefixGameTestTemplate(false)
public final class PlayerDropsTest {
    @GameTest(templateName="empty",batchId="player_drops",tickLimit=80)
    public void survivalDeathSuppressesOnlyItems(TestContext context) throws Exception {
        for(boolean enabled:new boolean[]{true,false})try(var f=new Fixture(context,enabled)){
            var player=f.player(GameMode.SURVIVAL);
            f.equip(player);
            context.assertTrue(player.damage(player.getDamageSources().genericKill(),1000),"Actual survival lethal damage rejected");
            f.assertDeath(player,enabled,false,70);
        }
        context.complete();
    }

    @GameTest(templateName="empty",batchId="player_drops",tickLimit=80)
    public void keepInventoryRemainsIndependent(TestContext context) throws Exception {
        for(boolean enabled:new boolean[]{true,false})try(var f=new Fixture(context,enabled)){
            f.keepInventory(true);
            var player=f.player(GameMode.SURVIVAL);
            f.equip(player);
            context.assertTrue(player.damage(player.getDamageSources().genericKill(),1000),"keepInventory lethal damage rejected");
            f.assertDeath(player,enabled,true,0);
            var replacement=f.map.world().getServer().getPlayerManager().respawnPlayer(player,false,net.minecraft.entity.Entity.RemovalReason.KILLED);
            replacement.networkHandler.player=replacement;
            context.assertTrue(replacement!=player&&replacement.getInventory().getStack(0).getCount()==3
                &&replacement.experienceLevel==10,"Vanilla respawn did not retain the keepInventory inventory/XP");
        }
        context.complete();
    }

    @GameTest(templateName="empty",batchId="player_drops",tickLimit=80)
    public void nativeFormConfirmedDeathUsesSurvivalRules(TestContext context) throws Exception {
        for(boolean enabled:new boolean[]{true,false})try(var f=new Fixture(context,enabled)){
            var player=f.player(GameMode.SURVIVAL);
            f.equip(player);
            f.pairNative(player);
            context.assertTrue(player.isSpectator(),"Native form must use the real held spectator state");
            f.snapshot(player,false);
            HostSession.tick(f.map.world().getServer(),f.map.host());
            HostSession.tick(f.map.world().getServer(),f.map.host());
            f.assertDeath(player,enabled,false,70);
            context.assertTrue(player.isSpectator(),"Confirmed death released native-form control");
        }
        context.complete();
    }

    @GameTest(templateName="empty",batchId="player_drops",tickLimit=80)
    public void genuineSpectatorKeepsVanillaDeathBehavior(TestContext context) throws Exception {
        try(var f=new Fixture(context,false)){
            var player=f.player(GameMode.SPECTATOR);
            f.equip(player);
            f.pairNative(player);
            f.snapshot(player,false);
            HostSession.tick(f.map.world().getServer(),f.map.host());
            context.assertTrue(deaths(player)==1&&!player.isAlive(),"Native spectator death was not confirmed");
            context.assertTrue(player.isSpectator()&&player.getInventory().getStack(0).getCount()==3,
                "Genuine spectator was treated as held survival");
            context.assertTrue(f.events(player).isEmpty()&&f.items(Items.DIAMOND)==0&&f.xp()==0,
                "Genuine spectator must retain vanilla's skipped death drop path");
        }
        context.complete();
    }

    @GameTest(templateName="empty",batchId="player_drops",tickLimit=80)
    public void voluntaryTossPacketStillSpawnsItems(TestContext context) throws Exception {
        try(var f=new Fixture(context,false)){
            var player=f.player(GameMode.SURVIVAL);
            f.equip(player);
            player.networkHandler.onPlayerAction(new PlayerActionC2SPacket(PlayerActionC2SPacket.Action.DROP_ITEM,BlockPos.ORIGIN,Direction.DOWN));
            context.assertTrue(player.getInventory().getStack(0).getCount()==2&&f.items(Items.DIAMOND)==1,
                "DROP_ITEM was suppressed by the player-death rule");
            player.networkHandler.onPlayerAction(new PlayerActionC2SPacket(PlayerActionC2SPacket.Action.DROP_ALL_ITEMS,BlockPos.ORIGIN,Direction.DOWN));
            context.assertTrue(player.getInventory().getStack(0).isEmpty()&&f.items(Items.DIAMOND)==3,
                "DROP_ALL_ITEMS was suppressed by the player-death rule");
            context.assertTrue(f.tosses.size()==2&&f.tosses.stream().noneMatch(ItemTossEvent::isCanceled),
                "Actual Q/drop-stack events were not both delivered uncanceled");
            context.assertTrue(f.events(player).isEmpty()&&deaths(player)==0&&f.xp()==0,"Voluntary toss entered the death path");
        }
        context.complete();
    }

    @GameTest(templateName="empty",batchId="player_drops",tickLimit=80)
    public void mobLootAndXpRemainEnabled(TestContext context) throws Exception {
        for(boolean enabled:new boolean[]{true,false})try(var f=new Fixture(context,enabled)){
            var attacker=f.player(GameMode.SURVIVAL);
            var zombie=EntityType.ZOMBIE.create(f.map.world());
            context.assertTrue(zombie!=null,"Zombie fixture creation failed");
            zombie.setAiDisabled(true);
            zombie.refreshPositionAndAngles(6.5,64.25,12.5,0,0);
            zombie.equipStack(EquipmentSlot.MAINHAND,new ItemStack(Items.EMERALD,3));
            zombie.setEquipmentDropChance(EquipmentSlot.MAINHAND,2);
            context.assertTrue(f.map.world().spawnEntity(zombie),"Zombie did not enter the actual world");
            context.assertTrue(zombie.damage(zombie.getDamageSources().playerAttack(attacker),1000),"Actual mob death rejected");
            var events=f.events(zombie);
            context.assertTrue(events.size()==1&&!events.getFirst().isCanceled(),"Player-only rule canceled actual mob drops");
            context.assertTrue(f.items(Items.EMERALD)==3&&f.xp()==5,"Mob equipment/XP changed: items="+f.items(Items.EMERALD)+" xp="+f.xp());
        }
        context.complete();
    }

    @GameTest(templateName="empty",batchId="player_drops",tickLimit=80)
    public void disconnectedAuthorityCannotSuppressDrops(TestContext context) throws Exception {
        try(var f=new Fixture(context,false)){
            var player=f.player(GameMode.SURVIVAL);
            f.equip(player);
            context.assertTrue(HostGameplayRules.suppressPlayerDrops(player),"Disabled rule was not active before disconnect");
            f.authority.link.close();
            context.assertTrue(!GoldCraft.gameplayRulesAvailable(),"Closed bridge retained gameplay authority");
            context.assertTrue(player.damage(player.getDamageSources().genericKill(),1000),"Disconnected-authority lethal damage rejected");
            f.assertDeath(player,true,false,70);
        }
        context.complete();
    }

    private static int deaths(ServerPlayerEntity player){return player.getStatHandler().getStat(Stats.CUSTOM.getOrCreateStat(Stats.DEATHS));}

    private static final class Fixture implements AutoCloseable {
        final TestContext context;
        final NavigationTest.Fixture map;
        final boolean originalKeepInventory,originalMobLoot;
        final List<Player> players=new ArrayList<>();
        final List<LivingDropsEvent> drops=new ArrayList<>();
        final List<ItemTossEvent> tosses=new ArrayList<>();
        final Consumer<LivingDropsEvent> dropListener=drops::add;
        final Consumer<ItemTossEvent> tossListener=tosses::add;
        Authority authority;
        long tick;

        Fixture(TestContext context,boolean enabled) throws Exception {
            this.context=context;
            context.assertTrue(field("serverLink").get(null)==null&&field("authorityLink").get(null)==null,
                "PlayerDropsTest requires an unconfigured isolated TestServer");
            SharedVitals.clear();HostSession.stopped();HostGameplayRules.reset();
            map=NavigationTest.fixture(context,"nav_fixture");
            var rules=map.world().getGameRules();
            originalKeepInventory=rules.getBoolean(GameRules.KEEP_INVENTORY);
            originalMobLoot=rules.getBoolean(GameRules.DO_MOB_LOOT);
            try {
                keepInventory(false);
                rules.get(GameRules.DO_MOB_LOOT).set(true,map.world().getServer());
                HostWorldReset.activate(map.world(),map.host().epoch());
                // Store the actual event object and inspect it after dispatch,
                // independent of registration order within LOWEST priority.
                NeoForge.EVENT_BUS.addListener(EventPriority.LOWEST,true,LivingDropsEvent.class,dropListener);
                NeoForge.EVENT_BUS.addListener(EventPriority.LOWEST,true,ItemTossEvent.class,tossListener);
                authority=new Authority(context,map.host(),enabled);
            } catch(Exception|Error failure){close();throw failure;}
        }

        void keepInventory(boolean keep){map.world().getGameRules().get(GameRules.KEEP_INVENTORY).set(keep,map.world().getServer());}
        ServerPlayerEntity player(GameMode mode){
            var uuid=UUID.randomUUID();
            var data=ConnectedClientData.createDefault(new GameProfile(uuid,"Drops"+uuid.toString().substring(0,8)),false);
            var player=new ServerPlayerEntity(map.world().getServer(),map.world(),data.gameProfile(),data.syncedOptions());
            var connection=new ClientConnection(NetworkSide.SERVERBOUND);
            var channel=new EmbeddedChannel(connection);
            players.add(new Player(uuid,channel));
            map.world().getServer().getPlayerManager().onPlayerConnect(connection,player,data);
            // PlayerManager initially loads new players in the overworld.
            player.teleport(map.world(),4.5,64.25,12.5,0,0);
            player.changeGameMode(mode);
            for(int i=0;i<61;i++)player.tick();
            player.playerTick();player.onTeleportationDone();
            context.assertTrue(player.getServerWorld()==map.world()&&player.isAlive(),"Real player did not enter the managed fixture world");
            return player;
        }
        void equip(ServerPlayerEntity player){
            player.getInventory().clear();player.getInventory().selectedSlot=0;
            player.getInventory().setStack(0,new ItemStack(Items.DIAMOND,3));
            player.addExperienceLevels(10);
            context.assertTrue(player.experienceLevel==10,"Fresh player XP fixture was not level 10");
        }
        void pairNative(ServerPlayerEntity player){
            snapshot(player,true);HostSession.paired(player.getUuid(),map.host().epoch());
            HostSession.tick(map.world().getServer(),map.host());
            player.onTeleportationDone();
        }
        void snapshot(ServerPlayerEntity player,boolean alive){
            int flags=16|(alive?1:0); // Paired, ordinary CS form; no MC-control flag.
            map.host().actors(new Wire.Writer().i64(map.host().epoch()).i64(++tick).f32(tick/20f).i32(0).i32(1)
                .i32(1).i32(4).i32(7).i32(2).i32(flags).i32(1).f32(alive?100:0).f32(0)
                .f32(4.5f*32).f32(-12.5f*32).f32(44).f32(0).f32(0).f32(0).f32(0).f32(0)
                .f32(-16).f32(-16).f32(-36).f32(16).f32(16).f32(36)
                .bytes(Wire.uuid(player.getUuid())).i32(1).i64(0).toByteArray());
        }
        List<LivingDropsEvent> events(net.minecraft.entity.Entity entity){return drops.stream().filter(e->e.getEntity()==entity).toList();}
        int items(Item item){
            int count=0;
            for(var entity:map.world().iterateEntities())if(!entity.isRemoved()&&entity instanceof ItemEntity drop&&drop.getStack().isOf(item))count+=drop.getStack().getCount();
            return count;
        }
        int xp(){
            int total=0;
            for(var entity:map.world().iterateEntities())if(!entity.isRemoved()&&entity instanceof ExperienceOrbEntity orb){
                // Equal-value orbs can merge on spawn. Count represents the
                // number of pickups, so a plain sum of Value would undercount.
                var nbt=new NbtCompound();orb.writeCustomDataToNbt(nbt);
                total+=nbt.getShort("Value")*nbt.getInt("Count");
            }
            return total;
        }
        void assertDeath(ServerPlayerEntity player,boolean enabled,boolean keep,int expectedXp){
            context.assertTrue(!player.isAlive()&&deaths(player)==1,"Expected exactly one real player death");
            context.assertTrue(player.getInventory().getStack(0).getCount()==(keep?3:0),"Death-drop rule changed the independent vanilla inventory rule");
            var events=events(player);
            context.assertTrue(events.size()==1,"Actual LivingDropsEvent missing or duplicated: "+events.size());
            var event=events.getFirst();
            int captured=event.getDrops().stream().filter(e->e.getStack().isOf(Items.DIAMOND)).mapToInt(e->e.getStack().getCount()).sum();
            context.assertTrue(captured==(keep?0:3)&&event.isCanceled()==!enabled,
                "Actual capture/cancel mismatch: captured="+captured+" canceled="+event.isCanceled()+" enabled="+enabled+" keep="+keep);
            context.assertTrue(items(Items.DIAMOND)==(enabled&&!keep?3:0),"Incorrect number of actual world death items");
            context.assertTrue(xp()==expectedXp,"Player item policy changed XP: expected="+expectedXp+" actual="+xp());
        }
        @Override public void close() throws Exception {
            try {
                NeoForge.EVENT_BUS.unregister(dropListener);NeoForge.EVENT_BUS.unregister(tossListener);
                for(var owned:players){
                    var player=map.world().getServer().getPlayerManager().getPlayer(owned.uuid());
                    try {if(player!=null){HostSession.disconnect(player);map.world().getServer().getPlayerManager().remove(player);}}
                    finally {owned.channel().finishAndReleaseAll();}
                }
            } finally {
                keepInventory(originalKeepInventory);
                map.world().getGameRules().get(GameRules.DO_MOB_LOOT).set(originalMobLoot,map.world().getServer());
                SharedVitals.clear();HostSession.stopped();HostGameplayRules.reset();
                if(authority!=null)authority.close();
            }
        }
    }
    private record Player(UUID uuid,EmbeddedChannel channel){}
    private static Field field(String name) throws ReflectiveOperationException {
        var field=GoldCraft.class.getDeclaredField(name);field.setAccessible(true);return field;
    }

    /** The only reflection installs/restores a genuinely authenticated link.
     * No production readiness flag, capability, policy, or event is fabricated.
     */
    private static final class Authority implements AutoCloseable {
        final Object previousServer,previousAuthority;
        final long previousGeneration;
        ServerSocket listener;
        Socket peer;
        BridgeLink link;
        Authority(TestContext context,HostWorldState host,boolean enabled) throws Exception {
            previousServer=field("serverLink").get(null);previousAuthority=field("authorityLink").get(null);
            previousGeneration=field("generation").getLong(null);
            try {
                byte[] session=Wire.uuid(UUID.randomUUID()),token=Wire.uuid(UUID.randomUUID());
                listener=new ServerSocket(0,1,InetAddress.getByName("127.0.0.1"));listener.setSoTimeout(3000);
                link=new BridgeLink(new BridgeLink.Config(listener.getLocalPort(),session,token,Wire.FABRIC_SERVER,Wire.HOST_SERVER));
                peer=listener.accept();peer.setSoTimeout(3000);
                var input=peer.getInputStream();
                byte[] header=input.readNBytes(Wire.HEADER_BYTES);
                context.assertTrue(header.length==Wire.HEADER_BYTES,"Incomplete bridge HELLO header");
                var helloHeader=Wire.header(ByteBuffer.wrap(header));
                context.assertTrue(helloHeader.type()==Wire.HELLO&&helloHeader.epoch()==0&&helloHeader.sequence()==1,"Invalid bridge HELLO");
                byte[] payload=input.readNBytes(helloHeader.size());
                context.assertTrue(payload.length==helloHeader.size(),"Incomplete bridge HELLO payload");
                var hello=new Wire.Reader(payload);
                context.assertTrue(Arrays.equals(hello.bytes(16),session)&&Arrays.equals(hello.bytes(16),token)
                    &&hello.i32()==Wire.FABRIC_SERVER&&(hello.i32()&Wire.GAMEPLAY_RULES_CAP)!=0,"Bridge credentials/role/capability mismatch");
                hello.finish();
                var output=peer.getOutputStream();
                output.write(Wire.frame(Wire.WELCOME,host.epoch(),1,new Wire.Writer().bytes(session).i32(Wire.HOST_SERVER)
                    .i32(Wire.GAMEPLAY_RULES_CAP).f32(Wire.UNITS_PER_BLOCK).f32(Wire.Y_OFFSET).toByteArray()).array());
                output.write(Wire.frame(Wire.GAMEPLAY_RULES,host.epoch(),2,new Wire.Writer().i64(host.epoch()).i64(1).i64(0)
                    .i32(0).i32(enabled?1:0).toByteArray()).array());
                output.flush();
                BridgeLink.IncomingBatch batch;
                long until=System.nanoTime()+3_000_000_000L;
                do {
                    batch=link.drainWithContext();
                    if(!batch.messages().isEmpty())break;
                    Thread.sleep(2);
                } while(System.nanoTime()<until);
                context.assertTrue(batch.connected()&&batch.generation()!=0&&batch.epoch()==host.epoch()&&batch.messages().size()==1,
                    "Authenticated policy delivery failed: "+link.lastError());
                var received=batch.messages().getFirst();
                context.assertTrue(received.generation()==batch.generation()&&received.epoch()==batch.epoch()
                    &&received.sequence()==2&&received.message().type()==Wire.GAMEPLAY_RULES,"Policy lost authenticated transport context");
                field("serverLink").set(null,link);field("authorityLink").set(null,link);field("generation").setLong(null,batch.generation());
                context.assertTrue(GoldCraft.gameplayRulesAvailable(),"Actual server bridge failed capability gating");
                HostGameplayRules.accept(host,received.message().payload());
            } catch(Exception|Error failure){close();throw failure;}
        }
        @Override public void close() throws Exception {
            try {if(link!=null)link.close();}
            finally {
                field("serverLink").set(null,previousServer);field("authorityLink").set(null,previousAuthority);field("generation").setLong(null,previousGeneration);
                try {if(peer!=null)peer.close();}
                finally {if(listener!=null)listener.close();}
            }
        }
    }
}
