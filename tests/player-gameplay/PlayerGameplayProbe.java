package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.HostWorldState;
import dev.goldcraft.bridge.Wire;
import java.util.UUID;
import net.minecraft.entity.EquipmentSlot;
import net.minecraft.entity.damage.*;
import net.minecraft.item.*;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;

public class PlayerGameplayProbe {
    private static int checks;
    private static long revision;
    private static final UUID UUID1=new UUID(1,2);
    private static void check(boolean condition){checks++;if(!condition)throw new AssertionError("check "+checks);}
    private static void snapshot(MinecraftServer server,boolean bound,int flags){
        int previous=GoldCraft.packets.size();PlayerGameplay.tick(server,a->bound);
        check(GoldCraft.packets.size()==previous+1);
        var r=new Wire.Reader(GoldCraft.packets.getLast());
        check(r.i64()==11);check(r.i64()==++revision);
        check(r.i32()==2&&r.i32()==3&&r.i32()==4&&r.i32()==5);
        check(java.util.Arrays.equals(r.bytes(16),Wire.uuid(UUID1)));check(r.i32()==flags);r.finish();
    }
    public static void main(String[] args){
        var server=new MinecraftServer();var player=new ServerPlayerEntity();
        GoldCraft.HOST_WORLD.actors.add(new HostWorldState.Actor(2,3,4,5,UUID1));
        GoldCraft.HOST_WORLD.actors.add(new HostWorldState.Actor(3,4,5,6,new UUID(0,0)));
        GoldCraft.HOST_WORLD.actors.add(new HostWorldState.Actor(4,5,0,0,new UUID(3,4)));
        PlayerGameplay.reset();snapshot(server,true,0);server.players.put(UUID1,player);
        snapshot(server,true,1);snapshot(server,false,0);player.creative=true;snapshot(server,true,3);
        player.equipment.put(EquipmentSlot.HEAD,new ItemStack("pumpkin","",0,EquipmentSlot.HEAD));
        snapshot(server,true,3);
        player.equipment.put(EquipmentSlot.MAINHAND,new ItemStack(new ArmorItem(),"armor",2,EquipmentSlot.MAINHAND));
        snapshot(server,true,3);
        player.equipment.put(EquipmentSlot.CHEST,new ItemStack(new ArmorItem(),"armor",8,EquipmentSlot.CHEST));
        snapshot(server,true,7);player.creative=false;snapshot(server,true,5);
        player.alive=false;snapshot(server,true,1);player.alive=true;player.removed=true;snapshot(server,true,0);
        player.removed=false;player.equipment.clear();
        player.equipment.put(EquipmentSlot.LEGS,new ItemStack("mod_armor","armor",3,EquipmentSlot.LEGS));
        snapshot(server,true,5);
        player.equipment.put(EquipmentSlot.LEGS,new ItemStack("cosmetic","speed",3,EquipmentSlot.LEGS));
        snapshot(server,true,1);server.players.clear();snapshot(server,true,0);
        GoldCraft.available=false;int count=GoldCraft.packets.size();PlayerGameplay.tick(server,a->true);check(GoldCraft.packets.size()==count);
        GoldCraft.available=true;GoldCraft.send=false;PlayerGameplay.tick(server,a->true);check(GoldCraft.packets.size()==count);
        for(var type:new DamageType[]{DamageTypes.PLAYER_ATTACK,DamageTypes.ARROW,DamageTypes.TRIDENT}){
            check(ActivePlayerDamage.eligible(new DamageSource(player,type,false),type==DamageTypes.PLAYER_ATTACK?1:2));
            check(!ActivePlayerDamage.eligible(new DamageSource(new Object(),type,false),1));
            check(!ActivePlayerDamage.eligible(new DamageSource(player,type,false),3));
        }
        for(String name:new String[]{"magic","indirect_magic","thorns","wither","fire","fall","explosion","drown"})
            check(!ActivePlayerDamage.eligible(new DamageSource(player,new DamageType(name),false),1));
        check(ActivePlayerDamage.eligible(new DamageSource(player,new DamageType("epic"),false),4));
        check(ActivePlayerDamage.eligible(new DamageSource(player,new DamageType("mod_attack"),true),1));
        check(!ActivePlayerDamage.eligible(new DamageSource(null,new DamageType("mod_attack"),true),1));
        System.out.println(checks+" production gameplay publication/armor/active-damage adapter checks passed");
    }
}
