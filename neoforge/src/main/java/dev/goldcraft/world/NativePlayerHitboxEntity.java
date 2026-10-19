package dev.goldcraft.world;

import net.neoforged.bus.api.IEventBus;
import net.neoforged.neoforge.registries.DeferredRegister;
import net.neoforged.neoforge.registries.DeferredHolder;
import net.neoforged.neoforge.event.entity.EntityAttributeCreationEvent;
import net.minecraft.entity.*;
import net.minecraft.entity.attribute.EntityAttributes;
import net.minecraft.entity.player.PlayerEntity;
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
    private static final TrackedData<Boolean> POTION_INVISIBLE=DataTracker.registerData(NativePlayerHitboxEntity.class,TrackedDataHandlerRegistry.BOOLEAN);
    private NativePlayerEntity body;
    private DamageSource activeDamage;
    private boolean damageRequested;
    private boolean following;
    private boolean nativeDeathClaimed;
    boolean claimNativeDeath(){
        if(nativeDeathClaimed)return false;
        nativeDeathClaimed=true;return true;
    }
    public NativePlayerEntity body(){return body;}
    @Override public float getHealth(){return body!=null&&!following?body.predictedHealth():super.getHealth();}
    @Override public boolean isAlive(){return body==null?super.isAlive():!isRemoved()&&body.isAlive();}
    @Override public void onDeath(DamageSource source){/* Native TakeDamage owns death and rewards. */}
    void inheritTimers(NativePlayerHitboxEntity previous){
        age=previous.age;timeUntilRegen=previous.timeUntilRegen;hurtTime=previous.hurtTime;lastDamageTaken=previous.lastDamageTaken;
    }
    public long hostEpoch(){return dataTracker.get(HOST_EPOCH);}
    public int hostSlot(){return dataTracker.get(HOST_SLOT);}
    public int hostSerial(){return dataTracker.get(HOST_SERIAL);}
    public boolean potionInvisible(){return dataTracker.get(POTION_INVISIBLE);}
    void potionInvisible(boolean value){dataTracker.set(POTION_INVISIBLE,value);}
    public int hostLife(){return dataTracker.get(HOST_LIFE);}
    boolean follows(NativePlayerEntity value){return body==value;}
    public static void initialize(IEventBus bus){
        ENTITIES.register(bus);
        bus.addListener((EntityAttributeCreationEvent event)->event.put(TYPE.get(),PlayerEntity.createPlayerAttributes().build()));
    }
    public NativePlayerHitboxEntity(EntityType<? extends NativePlayerHitboxEntity> type,World world){
        super(type,world);setInvisible(true);setNoGravity(true);
    }
    @Override protected void initDataTracker(DataTracker.Builder builder){
        super.initDataTracker(builder);builder.add(WIDTH,1f);builder.add(HEIGHT,2.25f);
        builder.add(HOST_EPOCH,0L);builder.add(HOST_SLOT,0);builder.add(HOST_SERIAL,0);builder.add(HOST_LIFE,0);builder.add(POTION_INVISIBLE,false);
    }
    public void follow(NativePlayerEntity value){
        body=value;value.hitbox(this);var dimensions=value.getDimensions(value.getPose());
        var actor=value.actor();
        dataTracker.set(HOST_EPOCH,value.epoch());dataTracker.set(HOST_SLOT,actor.slot());
        dataTracker.set(HOST_SERIAL,actor.serial());dataTracker.set(HOST_LIFE,actor.life());
        dataTracker.set(WIDTH,dimensions.width());dataTracker.set(HEIGHT,dimensions.height());
        refreshPositionAndAngles(value.getX(),value.getY(),value.getZ(),value.getYaw(),value.getPitch());
        following=true;
        try{
            if(!NativeHealth.prepare(this,actor))getAttributeInstance(EntityAttributes.GENERIC_MAX_HEALTH).setBaseValue(value.getMaxHealth());
            NativeHealth.synchronize(this,value.getHealth());
        }
        finally{following=false;}
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
        float absorptionBefore=getAbsorptionAmount();
        try(var effects=NativeHitEffects.begin(this)){
            // Run vanilla, NeoForge and Epic's actual hurt pipeline on the one
            // spatial target. Skills can reset its real cooldown and receive
            // normal hit feedback/damage modifiers. Only native health commits.
            boolean hit=super.damage(source,body.difficultyDamage(source,amount));
            effects.localResult(hit,getAbsorptionAmount()<absorptionBefore);
            // A hit paid entirely by golden hearts still landed, even though
            // it has no native health delta to send.
            return hit&&(damageRequested||getAbsorptionAmount()<absorptionBefore);
        }finally{activeDamage=previous;damageRequested=previousRequested;}
    }
    @Override public void setHealth(float value){
        if(activeDamage!=null){
            float amount=getHealth()-value;
            if(amount>0&&body!=null)damageRequested|=body.requestDamage(activeDamage,amount);
            return;
        }
        var status=following?null:NativeStatusExecution.current(this);
        if(status!=null&&body!=null&&Float.isFinite(value)){
            float amount=getHealth()-Math.clamp(value,0,Math.max(getHealth(),getMaxHealth()));
            if(amount!=0)body.requestDamage(getDamageSources().generic(),amount);
            return;
        }
        super.setHealth(value);
    }
    @Override public void tick(){
        // Native movement/environment own this body. Running LivingEntity.tick
        // would add a second gravity/suffocation simulation. Effects alone own
        // their normal vanilla/NeoForge timer and callbacks on this one body.
        age++;if(timeUntilRegen>0)timeUntilRegen--;if(hurtTime>0)hurtTime--;
        prevX=getX();prevY=getY();prevZ=getZ();prevYaw=getYaw();prevPitch=getPitch();
        if(!getWorld().isClient&&body!=null&&!body.isRemoved()&&body.isAlive()){
            tickStatusEffects();NativeNutrition.tick(this);setInvisible(true);
        }
    }
    @Override public void updateTrackedPositionAndAngles(double x,double y,double z,float yaw,float pitch,int steps){
        refreshPositionAndAngles(x,y,z,yaw,pitch);
    }
    @Override public boolean isPushable(){return false;}
    @Override public boolean isCollidable(){return isAlive();}
    @Override public void takeKnockback(double strength,double x,double z){
        if(body!=null&&!getWorld().isClient)NativeKnockback.apply(this,()->super.takeKnockback(strength,x,z));
    }
    @Override public boolean shouldSave(){return false;}
    @Override public Iterable<ItemStack> getArmorItems(){return List.of();}
    @Override public ItemStack getEquippedStack(EquipmentSlot slot){return ItemStack.EMPTY;}
    @Override public void equipStack(EquipmentSlot slot,ItemStack stack){}
    @Override public Arm getMainArm(){return Arm.RIGHT;}
}
