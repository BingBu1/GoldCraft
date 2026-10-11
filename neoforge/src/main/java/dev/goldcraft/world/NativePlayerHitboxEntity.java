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
    private static final TrackedData<Long> HOST_EPOCH=DataTracker.registerData(NativePlayerHitboxEntity.class,TrackedDataHandlerRegistry.LONG);
    private static final TrackedData<Integer> HOST_SLOT=DataTracker.registerData(NativePlayerHitboxEntity.class,TrackedDataHandlerRegistry.INTEGER);
    private static final TrackedData<Integer> HOST_SERIAL=DataTracker.registerData(NativePlayerHitboxEntity.class,TrackedDataHandlerRegistry.INTEGER);
    private static final TrackedData<Integer> HOST_LIFE=DataTracker.registerData(NativePlayerHitboxEntity.class,TrackedDataHandlerRegistry.INTEGER);
    private NativePlayerEntity body;
    private DamageSource activeDamage;
    private boolean damageRequested;
    public long hostEpoch(){return dataTracker.get(HOST_EPOCH);}
    public int hostSlot(){return dataTracker.get(HOST_SLOT);}
    public int hostSerial(){return dataTracker.get(HOST_SERIAL);}
    public int hostLife(){return dataTracker.get(HOST_LIFE);}
    boolean follows(NativePlayerEntity value){return body==value;}
    public static void initialize(IEventBus bus){
        ENTITIES.register(bus);
        bus.addListener((EntityAttributeCreationEvent event)->event.put(TYPE.get(),LivingEntity.createLivingAttributes().build()));
    }
    public NativePlayerHitboxEntity(EntityType<? extends NativePlayerHitboxEntity> type,World world){
        super(type,world);setInvisible(true);setNoGravity(true);
    }
    @Override protected void initDataTracker(DataTracker.Builder builder){
        super.initDataTracker(builder);builder.add(WIDTH,1f);builder.add(HEIGHT,2.25f);
        builder.add(HOST_EPOCH,0L);builder.add(HOST_SLOT,0);builder.add(HOST_SERIAL,0);builder.add(HOST_LIFE,0);
    }
    public void follow(NativePlayerEntity value){
        body=value;value.hitbox(this);var dimensions=value.getDimensions(value.getPose());
        var actor=value.actor();
        dataTracker.set(HOST_EPOCH,value.epoch());dataTracker.set(HOST_SLOT,actor.slot());
        dataTracker.set(HOST_SERIAL,actor.serial());dataTracker.set(HOST_LIFE,actor.life());
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
        if(getWorld().isClient||body==null||body.isRemoved()||!body.isAlive()||!Float.isFinite(amount)||amount<=0)return false;
        DamageSource previous=activeDamage;boolean previousRequested=damageRequested;
        activeDamage=source;damageRequested=false;
        try{
            // Run vanilla, NeoForge and Epic's actual hurt pipeline on the one
            // spatial target. Skills can reset its real cooldown and receive
            // normal hit feedback/damage modifiers. Only native health commits.
            return super.damage(source,body.difficultyDamage(source,amount))&&damageRequested;
        }finally{activeDamage=previous;damageRequested=previousRequested;}
    }
    @Override public void setHealth(float value){
        if(activeDamage!=null){
            float amount=getHealth()-value;
            if(amount>0&&body!=null)damageRequested|=body.requestDamage(activeDamage,amount);
            return;
        }
        super.setHealth(value);
    }
    @Override public void tick(){
        // Native movement/environment own this body. Running LivingEntity.tick
        // would add a second gravity, suffocation and status-effect simulation.
        age++;if(timeUntilRegen>0)timeUntilRegen--;if(hurtTime>0)hurtTime--;
        prevX=getX();prevY=getY();prevZ=getZ();prevYaw=getYaw();prevPitch=getPitch();
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
