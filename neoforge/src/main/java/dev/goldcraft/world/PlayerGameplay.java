package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import net.minecraft.entity.EquipmentSlot;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.item.ArmorItem;
import net.minecraft.item.ItemStack;
import net.minecraft.server.MinecraftServer;
import java.util.function.Predicate;

/** Independent of camera/control readiness: only the real server player counts. */
public final class PlayerGameplay {
    private static long revision;
    private PlayerGameplay(){}
    public static void reset(){revision=0;}
    static boolean armor(ItemStack stack,EquipmentSlot slot){
        if(slot.getType()!=EquipmentSlot.Type.HUMANOID_ARMOR||stack.isEmpty())return false;
        if(stack.getItem() instanceof ArmorItem)return true;
        // Component/mod equipment can supply real armor without subclassing
        // ArmorItem. A pumpkin or cosmetic head item alone is not armor.
        boolean[] defense={false};
        stack.applyAttributeModifiers(slot,(attribute,modifier)->{
            if(attribute.equals(EntityAttributes.GENERIC_ARMOR)&&modifier.value()>0)defense[0]=true;
        });
        return defense[0];
    }
    public static void tick(MinecraftServer server,Predicate<HostWorldState.Actor> bound){
        if(!GoldCraft.playerGameplayAvailable())return;
        var host=GoldCraft.HOST_WORLD;
        for(var actor:host.actors()){
            if(actor.minecraftPlayer().getMostSignificantBits()==0&&actor.minecraftPlayer().getLeastSignificantBits()==0)continue;
            if(actor.slot()<1||actor.slot()>64||actor.serial()==0||actor.spawn()==0||actor.life()==0)continue;
            var player=server.getPlayerManager().getPlayer(actor.minecraftPlayer());
            boolean connected=player!=null&&!player.isRemoved()&&bound.test(actor);
            int flags=connected?1|(player.isCreative()?2:0):0;
            if(connected&&player.isAlive()){
                for(var slot:EquipmentSlot.values())
                    if(armor(player.getEquippedStack(slot),slot)){flags|=4;break;}
            }
            var packet=new Wire.Writer().i64(host.epoch()).i64(++revision)
                .i32(actor.slot()).i32(actor.serial()).i32(actor.spawn()).i32(actor.life())
                .bytes(Wire.uuid(actor.minecraftPlayer())).i32(flags).toByteArray();
            if(!GoldCraft.sendPlayerGameplay(Wire.PLAYER_GAMEPLAY,packet))return;
        }
    }
}
