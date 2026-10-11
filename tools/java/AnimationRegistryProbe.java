package dev.goldcraft.animationprobe;

import com.google.gson.*;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import yesman.epicfight.api.animation.AnimationManager;
import java.nio.file.Files;
import java.util.*;

/** Inspect real registered animations; no rendering, player or recording transport. */
@Mod("goldcraft_animation_probe")
public final class AnimationRegistryProbe {
    public AnimationRegistryProbe() { NeoForge.EVENT_BUS.addListener(AnimationRegistryProbe::run); }

    static void run(ServerStartedEvent event) {
        JsonObject result=new JsonObject();
        JsonArray rows=new JsonArray();
        JsonArray failures=new JsonArray();
        JsonArray warnings=new JsonArray();
        try {
            var manager=AnimationManager.getInstance();
            var all=manager.getAnimations(a->true);
            result.addProperty("totalRegistered",all.size());
            var keys=new ArrayList<>(all.keySet());
            keys.sort(Comparator.comparing(Object::toString));
            for(var key:keys) {
                if(!Set.of("efn","epic_fight_avalon").contains(key.getNamespace()))continue;
                JsonObject row=new JsonObject();row.addProperty("id",key.toString());
                try {
                    var animation=all.get(key).get();
                    row.addProperty("class",animation.getClass().getName());
                    row.addProperty("resource",animation.getLocation().toString());
                    row.addProperty("meta",animation.isMetaAnimation());
                    if(!animation.isMetaAnimation()) {
                        var clip=animation.getAnimationClip();
                        row.addProperty("joints",clip.getJointTransforms().size());
                        row.addProperty("duration",clip.getClipTime());
                        if(!Float.isFinite(clip.getClipTime()))throw new AssertionError("Nonfinite clip duration");
                        if(clip.getJointTransforms().isEmpty())warnings.add(key+": empty clip");
                        row.addProperty("poseAtStart",clip.getPoseInTime(0).getJointTransformData().size());
                        row.addProperty("poseAtMidpoint",clip.getPoseInTime(clip.getClipTime()/2).getJointTransformData().size());
                    }
                } catch(Throwable t) {
                    row.addProperty("error",t.toString());failures.add(key+": "+t);t.printStackTrace();
                }
                rows.add(row);
            }
            JsonArray reported=JsonParser.parseString(Files.readString(FMLPaths.GAMEDIR.get().resolve("reported-animation-ids.json"))).getAsJsonArray();
            JsonObject matching=new JsonObject();
            for(var id:reported)matching.addProperty(id.getAsString(),AnimationManager.byKey(id.getAsString())!=null);
            result.add("reportedIdsRegistered",matching);
            result.addProperty("passed",rows.size()>0&&failures.isEmpty());
        } catch(Throwable t) {
            result.addProperty("passed",false);result.addProperty("error",t.toString());t.printStackTrace();
        }
        result.add("animations",rows);result.add("failures",failures);result.add("warnings",warnings);
        try { Files.writeString(FMLPaths.GAMEDIR.get().resolve("animation-registry.json"),result.toString()); }
        catch(Exception e){throw new RuntimeException(e);}
    }
}
