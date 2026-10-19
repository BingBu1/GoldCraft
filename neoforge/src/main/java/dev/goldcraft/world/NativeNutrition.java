package dev.goldcraft.world;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.mixin.NativeHungerAccessor;
import net.minecraft.entity.LivingEntity;
import net.minecraft.entity.player.HungerManager;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.ServerPlayerEntity;
import net.minecraft.world.GameMode;
import net.minecraft.world.Difficulty;
import net.minecraft.world.GameRules;
import java.util.Map;
import java.util.WeakHashMap;

/** One vanilla food state across a native spawn and its paired Minecraft body. */
public final class NativeNutrition {
    private record Step(MinecraftServer server,int tick){}
    private static final Map<HungerManager,Step> stepped=new WeakHashMap<>();
    private boolean initialized,running;
    private ServerPlayerEntity paired;

    void bind(NativePlayerEntity body,ServerPlayerEntity candidate){
        paired=candidate!=null&&!candidate.isRemoved()&&candidate.isAlive()
            &&candidate.getWorld()==body.getWorld()&&candidate.getUuid().equals(body.actor().minecraftPlayer())?candidate:null;
        if(!initialized){
            if(paired!=null)((NativeHungerAccessor)(Object)body).goldcraft$hunger(paired.getHungerManager());
            initialized=true;
            // Mark this manager even before its first native tick. Ordinary
            // players and Mod-owned updates remain outside our tick guard.
            stepped.putIfAbsent(body.getHungerManager(),new Step(null,0));
        }
        if(paired!=null&&paired.getHungerManager()!=body.getHungerManager())
            ((NativeHungerAccessor)paired).goldcraft$hunger(body.getHungerManager());
    }
    void inherit(NativePlayerEntity body,NativePlayerEntity previous){
        ((NativeHungerAccessor)(Object)body).goldcraft$hunger(previous.getHungerManager());
        initialized=previous.nutrition().initialized;
    }
    boolean running(){return running;}
    boolean exhaustionAllowed(){
        if(paired==null)return true;
        var mode=HostSession.gameplayMode(paired);
        return mode!=GameMode.CREATIVE&&mode!=GameMode.SPECTATOR;
    }
    private static boolean active(NativePlayerHitboxEntity box){
        var body=box.body();var host=GoldCraft.HOST_WORLD;
        if(box.isRemoved()||body==null||body.isRemoved()||!body.isAlive()||body.epoch()!=host.epoch())return false;
        var previous=body.actor();
        for(var actor:host.actors())if(actor.slot()==previous.slot())
            return actor.serial()==previous.serial()&&actor.spawn()==previous.spawn()&&actor.life()==previous.life()
                &&actor.life()!=0&&(actor.flags()&5)==1&&actor.health()>0&&(actor.team()==1||actor.team()==2)
                &&!actor.minecraftForm()&&NativePlayers.hitbox(actor)==box;
        return false;
    }
    /** Only Hunger/Saturation use this adapter; other effect callbacks keep their actual owner. */
    public static LivingEntity playerTarget(LivingEntity entity){
        return entity instanceof NativePlayerHitboxEntity box&&active(box)?box.body():entity;
    }
    private static boolean claim(HungerManager manager,PlayerEntity player){
        var server=player.getServer();if(server==null)return false;
        var previous=stepped.put(manager,new Step(server,server.getTicks()));
        return previous==null||previous.server()!=server||previous.tick()!=server.getTicks();
    }
    /** Called at the vanilla Player.tick food update, not from client HUD ticks. */
    public static boolean vanillaTick(HungerManager manager,PlayerEntity player){
        if(player instanceof ServerPlayerEntity paired){
            var actor=GoldCraft.HOST_WORLD.actor(paired.getUuid());
            if(actor!=null&&(!actor.minecraftForm()||NativeStatusEffects.waitingForHealth(paired)))return false;
        }
        return !stepped.containsKey(manager)||claim(manager,player);
    }
    public static boolean parked(PlayerEntity player){
        if(!(player instanceof ServerPlayerEntity paired))return false;
        var actor=GoldCraft.HOST_WORLD.actor(paired.getUuid());
        return actor!=null&&!actor.minecraftForm();
    }
    public static void tick(NativePlayerHitboxEntity box){
        if(!active(box))return;
        var body=box.body();var state=body.nutrition();
        if(!state.initialized||!claim(body.getHungerManager(),body))return;
        state.running=true;
        try(var scope=NativeStatusExecution.nutrition(box)){
            // Player.tickMovement also owns peaceful recovery. Native movement
            // does not run that method; the parked real player must not run this
            // recovery a second time against the same food object.
            var food=body.getHungerManager();
            if(body.getWorld().getDifficulty()==Difficulty.PEACEFUL
                &&body.getWorld().getGameRules().getBoolean(GameRules.NATURAL_REGENERATION)){
                if(box.age%20==0){
                    if(body.canFoodHeal())body.heal(1);
                    if(food.getSaturationLevel()<20)food.setSaturationLevel(food.getSaturationLevel()+1);
                }
                if(box.age%10==0&&food.isNotFull())food.setFoodLevel(food.getFoodLevel()+1);
            }
            // Vanilla owns exhaustion, saturation and starvation. The native
            // body denies canFoodHeal here, so neither food nor peaceful-mode
            // recovery adds CS health; active healing effects stay independent.
            food.update(body);
        }finally{state.running=false;}
    }
    public static void clear(){stepped.clear();}
}
