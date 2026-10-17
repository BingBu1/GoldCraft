package dev.goldcraft.projectileprobe;

import com.google.gson.JsonObject;
import dev.goldcraft.world.NativeProjectileDamage;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.ResourceLocation;
import net.minecraft.tags.TagKey;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.damagesource.DamageTypes;
import net.neoforged.fml.common.Mod;
import net.neoforged.fml.loading.FMLPaths;
import net.neoforged.neoforge.common.NeoForge;
import net.neoforged.neoforge.event.server.ServerStartedEvent;
import java.nio.file.Files;

@Mod("goldcraft_projectile_probe")
public final class OptionalProjectileProbe {
    public OptionalProjectileProbe(){NeoForge.EVENT_BUS.addListener(OptionalProjectileProbe::run);}
    static void run(ServerStartedEvent event){
        var report=new JsonObject();
        try{
            var registry=event.getServer().registryAccess().registryOrThrow(Registries.DAMAGE_TYPE);
            for(String name:new String[]{"active_player_attack","projectile_attack"}){
                var tag=registry.getTag(TagKey.create(Registries.DAMAGE_TYPE,ResourceLocation.fromNamespaceAndPath("goldcraft",name)));
                if(tag.isEmpty()||tag.get().size()!=0)throw new AssertionError("Optional TACZ entries should resolve to empty "+name);
            }
            if(!NativeProjectileDamage.isProjectile(new DamageSource(registry.getHolderOrThrow(DamageTypes.ARROW))))throw new AssertionError("Vanilla arrow lost projectile classification");
            if(NativeProjectileDamage.isProjectile(new DamageSource(registry.getHolderOrThrow(DamageTypes.INDIRECT_MAGIC))))throw new AssertionError("Magic became projectile");
            report.addProperty("checks",4);report.addProperty("passed",true);
        }catch(Throwable error){report.addProperty("passed",false);report.addProperty("error",error.toString());error.printStackTrace();}
        try{Files.writeString(FMLPaths.GAMEDIR.get().resolve("projectile-result.json"),report.toString());}catch(Exception e){throw new RuntimeException(e);}
    }
}
