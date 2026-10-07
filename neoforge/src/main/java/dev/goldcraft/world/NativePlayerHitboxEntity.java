package dev.goldcraft.world;

import net.neoforged.bus.api.IEventBus;
import net.neoforged.neoforge.registries.DeferredRegister;
import net.neoforged.neoforge.registries.DeferredHolder;
import net.neoforged.neoforge.event.entity.EntityAttributeCreationEvent;
import net.minecraft.entity.*;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.entity.damage.DamageSource;
import net.minecraft.entity.data.DataTracker;
import net.minecraft.entity.data.TrackedData;
import net.minecraft.entity.data.TrackedDataHandlerRegistry;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.RegistryKeys;
import net.minecraft.util.Arm;
import net.minecraft.util.Identifier;
import net.minecraft.world.World;
import java.util.List;

/**
 * Networked, invisible hit target for a real native player. The server-only
 * PlayerEntity remains the single vanilla AI target; GoldSrc draws its body.
 */
public final class NativePlayerHitboxEntity extends LivingEntity {
    private static final DeferredRegister<EntityType<?>> ENTITIES=DeferredRegister.create(RegistryKeys.ENTITY_TYPE,"goldcraft");
    public static final DeferredHolder<EntityType<?>,EntityType<NativePlayerHitboxEntity>> TYPE=ENTITIES.register("native_player_hitbox",()->
        EntityType.Builder.<NativePlayerHitboxEntity>create(NativePlayerHitboxEntity::new,SpawnGroup.MISC)
            .dimensions(1,2.25f).maxTrackingRange(16).trackingTickInterval(1).disableSaving().disableSummon()
            .build("goldcraft:native_player_hitbox"));
    private static final TrackedData<Float> WIDTH=DataTracker.registerData(NativePlayerHitboxEntity.class,TrackedDataHandlerRegistry.FLOAT);
    private static final TrackedData<Float> HEIGHT=DataTracker.registerData(NativePlayerHitboxEntity.class,TrackedDataHandlerRegistry.FLOAT);
    private NativePlayerEntity body;
    public static void initialize(IEventBus bus){
        ENTITIES.register(bus);
        bus.addListener((EntityAttributeCreationEvent event)->event.put(TYPE.get(),LivingEntity.createLivingAttributes().build()));
    }
    public NativePlayerHitboxEntity(EntityType<? extends NativePlayerHitboxEntity> type,World world){
        super(type,world);setInvisible(true);setNoGravity(true);
    }
    @Override protected void initDataTracker(DataTracker.Builder builder){
        super.initDataTracker(builder);builder.add(WIDTH,1f);builder.add(HEIGHT,2.25f);
    }
    public void follow(NativePlayerEntity value){
        body=value;var dimensions=value.getDimensions(value.getPose());
        dataTracker.set(WIDTH,dimensions.width());dataTracker.set(HEIGHT,dimensions.height());
        refreshPositionAndAngles(value.getX(),value.getY(),value.getZ(),value.getYaw(),value.getPitch());
        getAttributeInstance(EntityAttributes.GENERIC_MAX_HEALTH).setBaseValue(value.getMaxHealth());setHealth(value.getHealth());
    }
    @Override public EntityDimensions getBaseDimensions(EntityPose pose){
        if(dataTracker==null)return super.getBaseDimensions(pose);
        return EntityDimensions.fixed(dataTracker.get(WIDTH),dataTracker.get(HEIGHT));
    }
    @Override public void onTrackedDataSet(TrackedData<?> data){
        super.onTrackedDataSet(data);if(data==WIDTH||data==HEIGHT)calculateDimensions();
    }
    @Override public boolean damage(DamageSource source,float amount){
        return !getWorld().isClient&&body!=null&&!body.isRemoved()&&body.damage(source,amount);
    }
    @Override public void tick(){
        // Native movement/environment own this body. Running LivingEntity.tick
        // would add a second gravity, suffocation and status-effect simulation.
        age++;prevX=getX();prevY=getY();prevZ=getZ();prevYaw=getYaw();prevPitch=getPitch();
    }
    @Override public void updateTrackedPositionAndAngles(double x,double y,double z,float yaw,float pitch,int steps){
        refreshPositionAndAngles(x,y,z,yaw,pitch);
    }
    @Override public boolean isPushable(){return false;}
    @Override public boolean isCollidable(){return isAlive();}
    @Override public void takeKnockback(double strength,double x,double z){}
    @Override public boolean shouldSave(){return false;}
    @Override public Iterable<ItemStack> getArmorItems(){return List.of();}
    @Override public ItemStack getEquippedStack(EquipmentSlot slot){return ItemStack.EMPTY;}
    @Override public void equipStack(EquipmentSlot slot,ItemStack stack){}
    @Override public Arm getMainArm(){return Arm.RIGHT;}
}
