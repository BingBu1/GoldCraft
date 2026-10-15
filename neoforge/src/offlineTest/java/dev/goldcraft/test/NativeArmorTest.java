package dev.goldcraft.test;

import com.mojang.authlib.GameProfile;
import dev.goldcraft.world.NativeArmorDefense;
import io.netty.channel.embedded.EmbeddedChannel;
import net.minecraft.entity.EquipmentSlot;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.enchantment.Enchantment;
import net.minecraft.enchantment.Enchantments;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.damage.DamageTypes;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.item.Items;
import net.minecraft.network.ClientConnection;
import net.minecraft.network.NetworkSide;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.registry.RegistryKey;
import net.minecraft.entity.damage.DamageType;
import net.minecraft.server.network.ConnectedClientData;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.test.GameTest;
import net.minecraft.test.TestContext;
import net.minecraft.world.GameMode;
import net.neoforged.neoforge.gametest.GameTestHolder;
import net.neoforged.neoforge.gametest.PrefixGameTestTemplate;
import java.util.UUID;

/** Differential real vanilla hurt versus equipment-only native bridge stage. */
@GameTestHolder("goldcraft_tests")
@PrefixGameTestTemplate(false)
public final class NativeArmorTest {
    private static final EquipmentSlot[] SLOTS={EquipmentSlot.HEAD,EquipmentSlot.CHEST,EquipmentSlot.LEGS,EquipmentSlot.FEET};
    private static final Item[][] ARMOR={
        {Items.AIR,Items.AIR,Items.AIR,Items.AIR},
        {Items.IRON_HELMET,Items.IRON_CHESTPLATE,Items.IRON_LEGGINGS,Items.IRON_BOOTS},
        {Items.DIAMOND_HELMET,Items.DIAMOND_CHESTPLATE,Items.DIAMOND_LEGGINGS,Items.DIAMOND_BOOTS},
        {Items.NETHERITE_HELMET,Items.NETHERITE_CHESTPLATE,Items.NETHERITE_LEGGINGS,Items.NETHERITE_BOOTS}
    };
    private static final class Player implements AutoCloseable {
        final ServerPlayerEntity entity;
        final EmbeddedChannel channel;
        Player(TestContext context,int tier){
            Item[] armor=ARMOR[tier];
            var world=context.getWorld();var uuid=UUID.randomUUID();
            var data=ConnectedClientData.createDefault(new GameProfile(uuid,"Armor"+uuid.toString().substring(0,8)),false);
            entity=new ServerPlayerEntity(world.getServer(),world,data.gameProfile(),data.syncedOptions());
            var connection=new ClientConnection(NetworkSide.SERVERBOUND);channel=new EmbeddedChannel(connection);
            world.getServer().getPlayerManager().onPlayerConnect(connection,entity,data);
            entity.changeGameMode(GameMode.SURVIVAL);
            for(int i=0;i<SLOTS.length;i++)entity.equipStack(SLOTS[i],new ItemStack(armor[i]));
            // ServerPlayerEntity.tick only advances join immunity. The actual
            // network-player tick calls LivingEntity.tick and refreshes equipment.
            for(int i=0;i<61;i++)entity.tick();
            entity.playerTick();
            entity.onTeleportationDone();
            try {
                int[] armorValues={0,15,20,20};
                double[] toughnessValues={0,0,8,12};
                context.assertTrue(entity.getArmor()==armorValues[tier]
                    &&entity.getAttributeValue(EntityAttributes.GENERIC_ARMOR_TOUGHNESS)==toughnessValues[tier],
                    "Equipped attributes not initialized: tier="+tier+" "+state(entity));
                context.assertTrue(entity.isAlive()&&entity.getHealth()==20,
                    "Fixture initialization changed health: "+state(entity));
            } catch(RuntimeException|Error failure) {
                close();
                throw failure;
            }
        }
        @Override public void close(){entity.getServer().getPlayerManager().remove(entity);channel.finishAndReleaseAll();}
    }
    private static String state(ServerPlayerEntity player){
        var text=new StringBuilder("health=").append(player.getHealth())
            .append(" alive=").append(player.isAlive()).append(" armor=").append(player.getArmor())
            .append(" toughness=").append(player.getAttributeValue(EntityAttributes.GENERIC_ARMOR_TOUGHNESS));
        for(var slot:SLOTS){
            var stack=player.getEquippedStack(slot);
            text.append(' ').append(slot).append('=').append(stack.getItem())
                .append(':').append(stack.getDamage()).append('/').append(stack.getMaxDamage());
        }
        return text.toString();
    }
    @GameTest(templateName="empty",batchId="native_armor_equipment",tickLimit=80)
    public void actualArmorToughnessAndWearMatchVanilla(TestContext context){
        // These values are independent fixtures for eight and twenty MC damage.
        float[][] expected={{40,22.4f,11.2f,10.56f},{100,80,40,36}};
        for(int hit=0;hit<2;hit++)for(int tier=0;tier<ARMOR.length;tier++){
            try(var bridged=new Player(context,tier);var vanilla=new Player(context,tier)){
                float nativeDamage=hit==0?40:100;
                var entry=context.getWorld().getRegistryManager().get(RegistryKeys.DAMAGE_TYPE).entryOf(DamageTypes.PLAYER_ATTACK);
                var source=new DamageSource(entry);
                float initial=vanilla.entity.getHealth();
                var reduced=NativeArmorDefense.reduce(bridged.entity,source,nativeDamage);
                context.assertTrue(Math.abs(reduced.afterArmor()-expected[hit][tier])<.0001f,"Armor/toughness fixture mismatch: tier="+tier+" nativeHit="+nativeDamage
                        +" expected="+expected[hit][tier]+" actual="+reduced.afterArmor()+" "+state(bridged.entity));
                context.assertTrue(reduced.enchantmentMultiplier()==1,"Unenchanted equipment added protection");
                context.assertTrue(vanilla.entity.damage(source,nativeDamage/5),"Real vanilla reference rejected hit");
                float actual=(initial-vanilla.entity.getHealth())*5;
                context.assertTrue(Math.abs(reduced.afterArmor()-actual)<.0001f,"Bridge defense differs from actual vanilla hurt: tier="+tier+" nativeHit="+nativeDamage
                        +" bridge="+reduced.afterArmor()+" reference="+actual+" "+state(vanilla.entity));
                context.assertTrue(bridged.entity.getHealth()==20&&bridged.entity.getAbsorptionAmount()==0,
                    "Equipment stage changed health/absorption and would emit a duplicate native delta");
                for(var slot:SLOTS)context.assertTrue(bridged.entity.getEquippedStack(slot).getDamage()==vanilla.entity.getEquippedStack(slot).getDamage(),
                    "Equipment durability differs from real vanilla hurt: tier="+tier+" nativeHit="+nativeDamage
                        +" bridge "+state(bridged.entity)+" reference "+state(vanilla.entity));
                System.out.println("Armor fixture tier="+tier+" nativeHit="+nativeDamage+" afterArmor="+reduced.afterArmor()+" "+state(bridged.entity));
            }
        }
        context.complete();
    }
    private record ProtectionCase(RegistryKey<Enchantment> enchantment,RegistryKey<DamageType> source,float multiplier){}
    @GameTest(templateName="empty",batchId="native_armor_equipment",tickLimit=80)
    public void sourceSensitiveEnchantmentsMatchVanilla(TestContext context){
        var cases=new ProtectionCase[]{
            new ProtectionCase(Enchantments.PROTECTION,DamageTypes.PLAYER_ATTACK,.84f),
            new ProtectionCase(Enchantments.PROJECTILE_PROTECTION,DamageTypes.ARROW,.68f),
            new ProtectionCase(Enchantments.BLAST_PROTECTION,DamageTypes.EXPLOSION,.68f),
            new ProtectionCase(Enchantments.FIRE_PROTECTION,DamageTypes.IN_FIRE,.68f),
            new ProtectionCase(Enchantments.PROJECTILE_PROTECTION,DamageTypes.PLAYER_ATTACK,1)
        };
        for(var fixture:cases){
            try(var bridged=new Player(context,2);var vanilla=new Player(context,2)){
                var enchantment=context.getWorld().getRegistryManager().get(RegistryKeys.ENCHANTMENT).entryOf(fixture.enchantment());
                for(var player:new Player[]{bridged,vanilla})player.entity.getEquippedStack(EquipmentSlot.CHEST).addEnchantment(enchantment,4);
                var entry=context.getWorld().getRegistryManager().get(RegistryKeys.DAMAGE_TYPE).entryOf(fixture.source());
                var source=new DamageSource(entry);
                var reduction=NativeArmorDefense.reduce(bridged.entity,source,40);
                context.assertTrue(Math.abs(reduction.enchantmentMultiplier()-fixture.multiplier())<.0001f,"Wrong source-sensitive enchantment factor");
                context.assertTrue(vanilla.entity.damage(source,8),"Vanilla enchantment reference rejected hit");
                float expected=(20-vanilla.entity.getHealth())*5;
                context.assertTrue(Math.abs(expected-reduction.afterArmor()*reduction.enchantmentMultiplier())<.0001f,"Enchantments differ from real vanilla hurt");
                for(var slot:SLOTS)context.assertTrue(bridged.entity.getEquippedStack(slot).getDamage()==vanilla.entity.getEquippedStack(slot).getDamage(),"Enchanted durability differs");
            }
        }
        context.complete();
    }
    @GameTest(templateName="empty",batchId="native_armor_equipment",tickLimit=80)
    public void seededUnbreakingWearMatchesVanilla(TestContext context){
        var world=context.getWorld();
        var unbreaking=world.getRegistryManager().get(RegistryKeys.ENCHANTMENT).entryOf(Enchantments.UNBREAKING);
        var source=new DamageSource(world.getRegistryManager().get(RegistryKeys.DAMAGE_TYPE).entryOf(DamageTypes.PLAYER_ATTACK));
        for(int level:new int[]{1,3}){
            try(var bridged=new Player(context,2);var vanilla=new Player(context,2)){
                for(var player:new Player[]{bridged,vanilla})for(var slot:SLOTS)
                    player.entity.getEquippedStack(slot).addEnchantment(unbreaking,level);
                int unenchantedWear=0;
                for(int hit=0;hit<32;hit++){
                    float nativeDamage=hit%2==0?40:100;
                    unenchantedWear+=hit%2==0?2:5;
                    long seed=0x474f4c4443524146L+hit*104729L;
                    // 1.21.1 Enchantment.modifyValue(ITEM_DAMAGE) consumes the
                    // server world's RNG, not the wearer's RNG. Both calls are
                    // synchronous, with no world tick between seed and damage.
                    world.getRandom().setSeed(seed);
                    var reduction=NativeArmorDefense.reduce(bridged.entity,source,nativeDamage);
                    context.assertTrue(vanilla.entity.isAlive(),"Reference died before hit="+hit+" "+state(vanilla.entity));
                    vanilla.entity.setHealth(20);
                    vanilla.entity.timeUntilRegen=0;
                    world.getRandom().setSeed(seed);
                    context.assertTrue(vanilla.entity.damage(source,nativeDamage/5),
                        "Seeded vanilla reference rejected hit "+hit);
                    context.assertTrue(vanilla.entity.isAlive(),"Reference died on Unbreaking hit="+hit+" "+state(vanilla.entity));
                    context.assertTrue(Math.abs((20-vanilla.entity.getHealth())*5-reduction.afterArmor())<.0001f,
                        "Unbreaking changed armor reduction at hit "+hit);
                    context.assertTrue(reduction.enchantmentMultiplier()==1,
                        "Unbreaking incorrectly added damage protection");
                    for(var slot:SLOTS){
                        var actual=bridged.entity.getEquippedStack(slot);
                        var reference=vanilla.entity.getEquippedStack(slot);
                        context.assertTrue(!actual.isEmpty()&&!reference.isEmpty(),"Unexpected early armor break: level="+level+" hit="+hit
                                +" bridge "+state(bridged.entity)+" reference "+state(vanilla.entity));
                        context.assertTrue(actual.getDamage()==reference.getDamage(),
                            "Seeded Unbreaking wear differs: level "+level+", hit "+hit+", slot "+slot
                                +" bridge "+state(bridged.entity)+" reference "+state(vanilla.entity));
                    }
                    context.assertTrue(bridged.entity.getHealth()==20&&bridged.entity.getAbsorptionAmount()==0,
                        "Unbreaking equipment pass changed health/absorption");
                }
                int observedWear=0;
                for(var slot:SLOTS)observedWear+=bridged.entity.getEquippedStack(slot).getDamage();
                context.assertTrue(observedWear>0&&observedWear<unenchantedWear*SLOTS.length,
                    "Fixture must exercise both actual wear and Unbreaking savings at level "+level);
                System.out.println("Armor Unbreaking level="+level+" hits=32 unenchantedWear="
                    +(unenchantedWear*SLOTS.length)+" observedWear="+observedWear+" "+state(bridged.entity));
            }
        }
        context.complete();
    }
    @GameTest(templateName="empty",batchId="native_armor_equipment",tickLimit=80)
    public void brokenArmorAndCreativeWearMatchVanilla(TestContext context){
        try(var bridged=new Player(context,1);var vanilla=new Player(context,1)){
            for(var player:new Player[]{bridged,vanilla})for(var slot:SLOTS){
                var stack=player.entity.getEquippedStack(slot);stack.setDamage(stack.getMaxDamage()-1);
            }
            var source=new DamageSource(context.getWorld().getRegistryManager().get(RegistryKeys.DAMAGE_TYPE).entryOf(DamageTypes.PLAYER_ATTACK));
            var reduction=NativeArmorDefense.reduce(bridged.entity,source,40);
            context.assertTrue(vanilla.entity.damage(source,8),"Vanilla breakage reference rejected hit");
            context.assertTrue(Math.abs((20-vanilla.entity.getHealth())*5-reduction.afterArmor())<.0001f,"Breakage changed reduction order");
            for(var slot:SLOTS)context.assertTrue(bridged.entity.getEquippedStack(slot).isEmpty()&&vanilla.entity.getEquippedStack(slot).isEmpty(),"Broken armor was retained");
        }
        try(var player=new Player(context,2)){
            player.entity.changeGameMode(GameMode.CREATIVE);
            var source=new DamageSource(context.getWorld().getRegistryManager().get(RegistryKeys.DAMAGE_TYPE).entryOf(DamageTypes.PLAYER_ATTACK));
            context.assertTrue(NativeArmorDefense.reduce(player.entity,source,100).afterArmor()==0,"Creative player lost immunity");
            for(var slot:SLOTS)context.assertTrue(player.entity.getEquippedStack(slot).getDamage()==0,"Creative armor was worn");
        }
        context.complete();
    }
}
