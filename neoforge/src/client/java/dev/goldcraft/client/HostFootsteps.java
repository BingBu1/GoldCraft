package dev.goldcraft.client;

import com.google.gson.JsonObject;
import dev.goldcraft.world.HostMovement;
import net.minecraft.client.MinecraftClient;
import net.minecraft.entity.player.PlayerEntity;
import net.minecraft.sound.SoundCategory;
import net.minecraft.sound.SoundEvents;
import net.minecraft.util.math.Vec3d;
import java.util.IdentityHashMap;
import java.util.Map;

/** BSP support is air in the MC block grid, so vanilla cannot choose a step sound. */
public final class HostFootsteps {
    private static final Map<PlayerEntity,Step> steps=new IdentityHashMap<>();
    private static long epoch,played;
    private static final class Step {
        Vec3d position;
        double distance;
        int life;
        Step(PlayerEntity player,int generation){position=player.getPos();life=generation;}
    }
    private HostFootsteps(){}
    public static void tick(MinecraftClient client){
        var host=GoldCraftClient.HOST;
        if(epoch!=host.epoch()){steps.clear();epoch=host.epoch();}
        if(client.world==null||host.geometry()==null||!client.world.getRegistryKey().getValue().toString().equals(host.dimension())){steps.clear();return;}
        steps.keySet().removeIf(player->!client.world.getPlayers().contains(player));
        for(var player:client.world.getPlayers()){
            var actor=host.actor(player.getUuid());
            if(actor==null||(actor.flags()&32)==0){steps.remove(player);continue;}
            var state=steps.computeIfAbsent(player,p->new Step(p,actor.life()));
            Vec3d delta=player.getPos().subtract(state.position);state.position=player.getPos();
            boolean ladder=HostMovement.ladder(player)!=null;
            // Real MC blocks/water retain their own vanilla sounds; no doubled steps.
            if(state.life!=actor.life()||delta.lengthSquared()>4||!player.isAlive()||player.isSpectator()
                ||player.isSilent()||player.isSneaking()||player.getAbilities().flying||player.hasVehicle()
                ||player.isTouchingWater()||!player.getSteppingBlockState().isAir()
                ||(!ladder&&(!player.isOnGround()||!HostMovement.supported(player)))){
                state.distance=0;state.life=actor.life();continue;
            }
            state.distance+=ladder?delta.length():delta.horizontalLength();
            double stride=ladder?1.2:1.0/0.6;
            if(state.distance>=stride){
                state.distance%=stride;
                // Native surface material mapping remains separate; stone is the
                // explicit fallback for host floor, and host ladders use ladder steps.
                client.world.playSound(player.getX(),player.getY(),player.getZ(),
                    ladder?SoundEvents.BLOCK_LADDER_STEP:SoundEvents.BLOCK_STONE_STEP,
                    SoundCategory.PLAYERS,0.15f,1.0f,false);
                played++;
            }
        }
    }
    public static void diagnostics(JsonObject data){data.addProperty("hostFootsteps",played);}
}
