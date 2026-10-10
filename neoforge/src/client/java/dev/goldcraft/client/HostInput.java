package dev.goldcraft.client;

import dev.goldcraft.GoldCraft;
import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.Wire;
import dev.goldcraft.net.ControlPayload;
import net.neoforged.neoforge.network.PacketDistributor;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.option.KeyBinding;
import net.minecraft.client.util.InputUtil;
import com.google.gson.JsonObject;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/** CS commands drive normal Minecraft controls; the Minecraft server validates movement/actions. */
public final class HostInput {
    private static BridgeLink link;
    private static long epoch,sequence,lastInput,lastReady;
    private static int buttons,presses,active;
    private static float pitch,yaw,forward,side;
    private static boolean controlling;
    private static int serial,life;
    private static net.minecraft.client.network.ClientPlayerEntity playerInstance;
    private static long lastDiagnostic;
    private HostInput(){}
    public static void configure(BridgeLink value){link=value;}
    public static boolean controlling(){return controlling;}
    public static boolean hosted(){
        var client=MinecraftClient.getInstance();
        return link!=null&&link.connected()&&client.player!=null&&client.world!=null
            &&GoldCraftClient.HOST.actor(client.player.getUuid())!=null
            &&client.world.getRegistryKey().getValue().toString().equals(GoldCraftClient.HOST.dimension());
    }
    public static int life(){return life;}
    public static boolean active(){return link!=null&&link.connected()&&active!=0&&System.nanoTime()-lastInput<250_000_000L;}
    public static boolean attacking(){return controlling&&active()&&(buttons&1)!=0;}
    public static void clear(){
        HostKeys.reset();
        HostMiningInput.clear();
        if(controlling){var o=MinecraftClient.getInstance().options;for(var key:new KeyBinding[]{o.forwardKey,o.backKey,o.leftKey,o.rightKey,o.jumpKey,o.sneakKey,o.sprintKey,o.attackKey,o.useKey})key.setPressed(false);}
        epoch=sequence=lastInput=lastReady=0;buttons=presses=active=serial=life=0;controlling=false;playerInstance=null;
    }
    public static void input(byte[] payload) {
        Wire.Reader r=new Wire.Reader(payload);long world=r.i64(),next=r.i64();r.f32();int enabled=r.i32(),keys=r.i32();
        float p=r.f32(),y=r.f32(),f=r.f32(),s=r.f32();r.f32();r.finish();
        if(world!=GoldCraftClient.HOST.epoch()||next<=sequence&&world==epoch)return;
        if(enabled<0||enabled>1||Math.abs(p)>180||Math.abs(y)>360000||Math.abs(f)>10000||Math.abs(s)>10000)throw new IllegalArgumentException("Host input bounds");
        if(world!=epoch){clear();epoch=world;}
        sequence=next;lastInput=System.nanoTime();active=enabled;presses|=keys&~buttons;buttons=keys;pitch=p;yaw=y;forward=f;side=s;
    }
    public static void action(byte[] payload) {
        Wire.Reader r=new Wire.Reader(payload);long world=r.i64();int code=r.i32();r.finish();
        MinecraftClient client=MinecraftClient.getInstance();if((!controlling&&code!=14&&code!=15)||world!=epoch||client.player==null)return;
        if(code>=1&&code<=9)client.player.getInventory().selectedSlot=code-1;
        else if(code==10){
            if(client.currentScreen instanceof net.minecraft.client.gui.screen.ingame.HandledScreen<?>)client.currentScreen.close();
            else if(client.currentScreen==null)press(client.options.inventoryKey);
        }
        else if(code==11)press(client.options.dropKey);
        else if(code==12)client.player.getInventory().selectedSlot=(client.player.getInventory().selectedSlot+1)%9;
        else if(code==13)client.player.getInventory().selectedSlot=(client.player.getInventory().selectedSlot+8)%9;
        else if(code==16&&client.currentScreen==null)press(client.options.pickItemKey);
        else if(code==17&&client.currentScreen==null)press(client.options.commandKey);
        else if(code==14&&client.currentScreen!=null&&client.currentScreen.shouldCloseOnEsc())client.currentScreen.close();
        else if(code==15) {
            GoldCraft.LOGGER.info("Minecraft resource reload requested from CS");
            client.reloadResources().whenComplete((ignored,error)-> {
                if(error==null)GoldCraft.LOGGER.info("Minecraft resource reload completed for CS");
                else GoldCraft.LOGGER.warn("Minecraft resource reload failed",error);
            });
        }
    }
    private static void press(KeyBinding key){KeyBinding.onKeyPressed(InputUtil.fromTranslationKey(key.getBoundKeyTranslationKey()));}
    public static void tick(MinecraftClient client) {
        var actor=client.player==null?null:GoldCraftClient.HOST.actor(client.player.getUuid());
        boolean changed=actor!=null&&(serial!=actor.serial()||life!=actor.life()||playerInstance!=client.player);
        if(changed){HostKeys.reset();serial=actor.serial();life=actor.life();playerInstance=client.player;lastReady=0;presses=0;}
        boolean ready=link!=null&&link.connected()&&GoldCraftClient.HOST.geometry()!=null&&actor!=null&&client.player.isAlive()
            &&client.world.getRegistryKey().getValue().toString().equals(GoldCraftClient.HOST.dimension())
            &&actor.minecraftForm()&&actor.life()!=0&&(actor.flags()&1)!=0&&(actor.flags()&4)==0&&epoch==GoldCraftClient.HOST.epoch()&&System.nanoTime()-lastInput<500_000_000L;
        if(MinecraftClient.getInstance().getNetworkHandler()!=null&&MinecraftClient.getInstance().getNetworkHandler().hasChannel(ControlPayload.ID)&&(System.nanoTime()-lastReady>400_000_000L||!ready&&controlling)) {
            PacketDistributor.sendToServer(new ControlPayload(GoldCraftClient.HOST.epoch(),serial,life,ready));lastReady=System.nanoTime();
        }
        boolean wasControlling=controlling;controlling=ready&&(actor.flags()&32)!=0&&!changed;
        if(!controlling||!active()||client.currentScreen!=null)HostKeys.releaseAll();
        if(wasControlling&&!controlling&&actor!=null&&!actor.minecraftForm()&&client.currentScreen instanceof net.minecraft.client.gui.screen.ingame.HandledScreen<?>)client.player.closeHandledScreen();
        if(!controlling&&!wasControlling)return;
        boolean move=controlling&&active()&&!GoldCraftClient.HOST.freeze()&&client.currentScreen==null;
        var options=client.options;
        options.forwardKey.setPressed(move&&forward>0);options.backKey.setPressed(move&&forward<0);
        options.leftKey.setPressed(move&&side<0);options.rightKey.setPressed(move&&side>0);
        options.jumpKey.setPressed(move&&(buttons&2)!=0);options.sneakKey.setPressed(move&&(buttons&4)!=0);
        options.sprintKey.setPressed(move&&(buttons&4096)!=0);
        options.attackKey.setPressed(move&&(buttons&1)!=0);options.useKey.setPressed(move&&(buttons&2048)!=0);
        if(move) {
            client.player.setYaw(-yaw-90);client.player.setPitch(pitch);
            client.player.prevYaw=client.player.getYaw();client.player.prevPitch=pitch;
            client.gameRenderer.updateCrosshairTarget(1);
            if((presses&1)!=0&&!HostKeys.attackEvent())press(options.attackKey);if((presses&2048)!=0&&!HostKeys.useEvent())press(options.useKey);
        }
        presses=0;
        HostKeys.endTick();
        if(controlling&&!wasControlling)GoldCraft.LOGGER.info("CS input now drives the authoritative Minecraft player");
    }
    public static void sendPose(MinecraftClient client) {
        if(link==null||!link.connected()||client.player==null||epoch==0)return;
        var p=client.player;int flags=(controlling?1:0)|(client.currentScreen!=null?8:0);
        link.send(Wire.PLAYER_POSE,new Wire.Writer().i64(epoch).i64(sequence).i64(System.nanoTime()).i32(flags).i32(life)
            .f32((float)p.getX()).f32((float)p.getY()).f32((float)p.getZ()).f32(p.getStandingEyeHeight()).toByteArray());
    }
    /** Optional sandbox observation; exposes routing/menu state without authentication material. */
    public static void writeDiagnostics(MinecraftClient client,HudExporter hud) {
        String nativeStatus=System.getenv("GOLDCRAFT_CLIENT_STATUS");
        if(nativeStatus==null||System.nanoTime()-lastDiagnostic<250_000_000L)return;
        lastDiagnostic=System.nanoTime();
        JsonObject data=new JsonObject();
        data.addProperty("world",Long.toUnsignedString(epoch));data.addProperty("inputSequence",sequence);
        data.addProperty("serial",serial);data.addProperty("life",life);
        data.addProperty("controlling",controlling);data.addProperty("inputActive",active!=0);
        var actor=client.player==null?null:GoldCraftClient.HOST.actor(client.player.getUuid());
        data.addProperty("minecraftForm",actor!=null&&actor.minecraftForm());
        data.addProperty("inputButtons",buttons);data.addProperty("forward",forward);data.addProperty("side",side);
        data.addProperty("freeze",GoldCraftClient.HOST.freeze());
        data.addProperty("mapMiningMode",GoldCraftClient.HOST.mining().mode());
        data.addProperty("mapMiningRevision",Long.toUnsignedString(GoldCraftClient.HOST.mining().revision()));
        data.addProperty("screen",client.currentScreen==null?"":client.currentScreen.getClass().getName());
        data.addProperty("chatScreen",client.currentScreen instanceof net.minecraft.client.gui.screen.ChatScreen);
        data.addProperty("operatorCommands",client.player!=null&&client.player.hasPermissionLevel(2));
        data.addProperty("overlay",client.getOverlay()==null?"":client.getOverlay().getClass().getName());
        if(client.player!=null)data.addProperty("player",client.player.getUuid().toString());
        hud.diagnostics(data);
        ParticleExporter.diagnostics(data);
        EntityExporter.diagnostics(data);
        BlockFeedbackExporter.diagnostics(data);
        HostCamera.diagnostics(data);
        HostKeys.diagnostics(data);
        AnimationDiagnostics.sample(client.player,data);
        GoldCraftClient.presentationDiagnostics(data);
        HostAudio.diagnostics(client,data);
        HostFootsteps.diagnostics(data);
        if(client.player!=null){
            var position=new com.google.gson.JsonArray();position.add(client.player.getX());position.add(client.player.getY());position.add(client.player.getZ());data.add("position",position);
            data.addProperty("entityId",client.player.getId());
            var ladder=dev.goldcraft.world.HostMovement.ladder(client.player);
            data.addProperty("hostLadderModel",ladder==null?0:ladder.model());
            data.addProperty("hostLadderCount",GoldCraftClient.HOST.brushes().stream().filter(b->b.ladder()).count());
            data.addProperty("hostHull",dev.goldcraft.world.HostMovement.hull(client.player));
            data.addProperty("hostClipMoves",dev.goldcraft.world.HostMovement.constrainedMoves);
            data.addProperty("hostStuckMoves",dev.goldcraft.world.HostMovement.stuckMoves);
            data.addProperty("hostRecoveryMoves",dev.goldcraft.world.HostMovement.recoveredMoves);
            data.addProperty("climbing",client.player.isClimbing());
            data.addProperty("horizontalCollision",client.player.horizontalCollision);
            data.addProperty("onGround",client.player.isOnGround());
            data.addProperty("pose",client.player.getPose().name());
            data.addProperty("selectedSlot",client.player.getInventory().selectedSlot);
            data.addProperty("selectedItem",net.minecraft.registry.Registries.ITEM.getId(client.player.getMainHandStack().getItem()).toString());
            data.addProperty("selectedCount",client.player.getMainHandStack().getCount());
            data.addProperty("offhandItem",net.minecraft.registry.Registries.ITEM.getId(client.player.getOffHandStack().getItem()).toString());
            data.addProperty("usingItem",client.player.isUsingItem());
            data.addProperty("itemUseTime",client.player.getItemUseTime());
            data.addProperty("swinging",client.player.handSwinging);
            data.addProperty("swingProgress",client.player.getHandSwingProgress(client.getRenderTickCounter().getTickDelta(false)));
        }
        if(client.currentScreen instanceof net.minecraft.client.gui.screen.ingame.HandledScreen<?> screen) {
            var bounds=(dev.goldcraft.client.mixin.HandledScreenAccessor)screen;
            var slots=new com.google.gson.JsonArray();
            for(var slot:screen.getScreenHandler().slots){
                var entry=new JsonObject();entry.addProperty("id",slot.id);entry.addProperty("x",bounds.goldcraft$x()+slot.x+8);entry.addProperty("y",bounds.goldcraft$y()+slot.y+8);
                entry.addProperty("item",net.minecraft.registry.Registries.ITEM.getId(slot.getStack().getItem()).toString());entry.addProperty("count",slot.getStack().getCount());slots.add(entry);
            }
            data.add("menuSlots",slots);data.addProperty("cursorCount",screen.getScreenHandler().getCursorStack().getCount());
        }
        try {
            Path target=Path.of(nativeStatus).resolveSibling("minecraft-client-status.json"),temporary=target.resolveSibling(target.getFileName()+".tmp");
            Files.writeString(temporary,data.toString());Files.move(temporary,target,StandardCopyOption.REPLACE_EXISTING);
        }catch(IOException ignored){/* Optional diagnostics cannot stop gameplay. */}
    }
}
