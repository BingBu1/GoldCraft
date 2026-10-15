package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.NativeArmorProtocol;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.Entity;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.damage.DamageTypes;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.math.Vec3d;
import java.util.ArrayDeque;
import java.util.LinkedHashMap;

/** Game-thread handler; registration/negotiation belongs to the future82 bundle. */
public final class NativeArmorServer {
    private static final int LIMIT=512;
    private record Completed(NativeArmorProtocol.Request request,byte[] reply){}
    private static final LinkedHashMap<Long,Completed> completed=new LinkedHashMap<>();
    private static final ArrayDeque<byte[]> replies=new ArrayDeque<>();
    private static long epoch,latest;
    private NativeArmorServer(){}
    public static void clear(){completed.clear();replies.clear();epoch=latest=0;}
    public static void flush(boolean negotiated){
        if(!negotiated)return;
        while(!replies.isEmpty()){
            if(!GoldCraft.sendToHost(NativeArmorProtocol.RESULT,replies.peek()))return;
            replies.remove();
        }
    }
    public static void handle(MinecraftServer server,byte[] bytes,boolean negotiated){
        if(!negotiated)throw new IllegalArgumentException("Unnegotiated native armor request");
        var q=NativeArmorProtocol.read(bytes);
        if(q.world()!=GoldCraft.HOST_WORLD.epoch())return;
        if(epoch!=q.world()){clear();epoch=q.world();}
        var prior=completed.get(q.event());
        if(prior!=null){
            if(!prior.request().equals(q))throw new IllegalArgumentException("Armor event changed on replay");
            if(replies.size()<LIMIT)replies.add(prior.reply());
            return;
        }
        if(Long.compareUnsigned(q.event(),latest)<=0)return;
        // Never wear equipment if the bounded response queue cannot retain the
        // result. Native authority settles its own timed-out pending request.
        if(replies.size()>=LIMIT)return;
        latest=q.event();
        byte[] result=q.result(1,q.damage(),1);
        var actor=GoldCraft.HOST_WORLD.actors().stream().filter(a->a.slot()==q.slot()
            &&a.serial()==q.serial()&&a.spawn()==q.spawn()&&a.life()==q.life()
            &&a.minecraftForm()&&(a.flags()&5)==1&&a.health()>0).findFirst().orElse(null);
        var wearer=actor==null?null:server.getPlayerManager().getPlayer(actor.minecraftPlayer());
        if(wearer!=null&&wearer.isAlive()){
            Entity attacker=null;
            if(q.attacker()!=0){
                var source=GoldCraft.HOST_WORLD.actors().stream().filter(a->a.slot()==q.attacker()
                    &&a.serial()==q.attackerSerial()&&a.spawn()==q.attackerSpawn()).findFirst().orElse(null);
                if(source!=null)attacker=NativePlayers.attacker(server,GoldCraft.HOST_WORLD,source);
            }
            var kind=switch(q.kind()){
                case 1->DamageTypes.PLAYER_ATTACK;
                case 2->DamageTypes.ARROW;
                case 3->DamageTypes.EXPLOSION;
                case 4->DamageTypes.IN_FIRE;
                case 5->DamageTypes.FALL;
                case 6->DamageTypes.DROWN;
                case 7->DamageTypes.MAGIC;
                default->throw new IllegalArgumentException("Unknown native armor kind");
            };
            var entry=wearer.getRegistryManager().get(RegistryKeys.DAMAGE_TYPE).entryOf(kind);
            var position=new Vec3d(q.x()/Wire.UNITS_PER_BLOCK,q.z()/Wire.UNITS_PER_BLOCK+Wire.Y_OFFSET,-q.y()/Wire.UNITS_PER_BLOCK);
            // A CS gun/knife must not inherit enchantments from a paired
            // attacker's stored MC main-hand item. Attacker identity remains.
            var source=new DamageSource(entry,attacker,attacker,position){
                @Override public ItemStack getWeaponStack(){return null;}
            };
            try{
                var defense=NativeArmorDefense.reduce(wearer,source,q.damage());
                result=q.result(0,defense.afterArmor(),defense.enchantmentMultiplier());
            }catch(RuntimeException error){
                GoldCraft.LOGGER.warn("Native equipment defense failed for slot {} event {}; native fallback required",
                    q.slot(),Long.toUnsignedString(q.event()),error);
            }
        }
        completed.put(q.event(),new Completed(q,result));
        while(completed.size()>LIMIT)completed.remove(completed.keySet().iterator().next());
        replies.add(result);
    }
}
