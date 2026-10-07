package dev.goldcraft.client;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.sound.SoundInstance;
import net.minecraft.client.sound.SoundListenerTransform;
import net.minecraft.sound.SoundCategory;
import com.sun.jna.Pointer;
import com.sun.jna.platform.win32.User32;
import org.lwjgl.glfw.GLFWNativeWin32;
import org.lwjgl.system.Platform;
import java.util.LinkedHashMap;
import java.util.Map;

/** Keep vanilla spatial audio and resource packs, with one audible local host pair. */
public final class HostAudio {
    private static boolean managed;
    private static volatile boolean audible=true;
    private static volatile float effectiveGain;
    private static volatile SoundListenerTransform listener=SoundListenerTransform.DEFAULT;
    private static final Map<String,Long> events=new LinkedHashMap<>();
    private static long focusChanges,acceptedSounds;
    private HostAudio(){}

    public static void configure(){managed=true;audible=false;}

    public static boolean standaloneFocused(MinecraftClient client){
        if(Platform.get()!=Platform.WINDOWS)return client.isWindowFocused();
        var foreground=User32.INSTANCE.GetForegroundWindow();
        return foreground!=null&&Pointer.nativeValue(foreground.getPointer())==GLFWNativeWin32.glfwGetWin32Window(client.getWindow().getHandle());
    }

    public static void frame(MinecraftClient client){
        if(!managed)return;
        // Minecraft initializes its cached windowFocused flag to true even if
        // a background startup never received focus. Use the real window owner.
        boolean standalone=standaloneFocused(client);
        boolean next=standalone||HostInput.active();
        if(next!=audible){
            audible=next;focusChanges++;
            // Refresh OpenAL gain without changing the user's volume setting or
            // stopping looped sources that must resume when this pair gains focus.
            client.getSoundManager().updateSoundVolume(SoundCategory.MASTER,client.options.getSoundVolume(SoundCategory.MASTER));
        }
        if(HostInput.hosted()&&!standalone&&client.world!=null&&client.player!=null){
            var entity=client.getCameraEntity();if(entity==null)entity=client.player;
            // Also run when the exported HUD/hand is disabled or rendering is skipped.
            client.gameRenderer.getCamera().update(client.world,entity,
                !client.options.getPerspective().isFirstPerson(),client.options.getPerspective().isFrontView(),
                client.getRenderTickCounter().getTickDelta(false));
        }
    }

    public static float gain(float configured){
        float result=managed&&!audible?0:configured;
        effectiveGain=result;
        return result;
    }
    public static void listener(SoundListenerTransform value){listener=value;}
    public static void accepted(SoundInstance sound){
        if(System.getenv("GOLDCRAFT_CLIENT_STATUS")==null)return;
        acceptedSounds++;
        String id=sound.getId().toString();
        if(events.size()<256||events.containsKey(id))events.merge(id,1L,Long::sum);
    }
    public static void diagnostics(MinecraftClient client,JsonObject data){
        data.addProperty("audioManaged",managed);data.addProperty("audioAudible",audible);
        data.addProperty("standaloneReportedFocused",client.isWindowFocused());
        data.addProperty("audioMasterVolume",client.options.getSoundVolume(SoundCategory.MASTER));
        data.addProperty("audioEffectiveGain",effectiveGain);data.addProperty("audioFocusChanges",focusChanges);
        data.addProperty("audioAcceptedSounds",acceptedSounds);data.addProperty("audioSources",client.getSoundManager().getDebugString());
        JsonObject counts=new JsonObject();events.forEach(counts::addProperty);data.add("audioEvents",counts);
        var transform=listener;JsonArray pos=new JsonArray(),forward=new JsonArray();
        pos.add(transform.position().x);pos.add(transform.position().y);pos.add(transform.position().z);
        forward.add(transform.forward().x);forward.add(transform.forward().y);forward.add(transform.forward().z);
        data.add("audioListener",pos);data.add("audioForward",forward);
    }
}
