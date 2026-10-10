package com.ysmef.geomodel.model.runtime;

import com.google.gson.*;
import com.ysmef.geomodel.YSMGeoModel;
import com.ysmef.geomodel.model.EFMeshJsonWriter;
import com.ysmef.geomodel.ysm.script.ScriptAnim;
import com.ysmef.geomodel.ysm.script.ScriptJson;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.zip.*;

/** Generates test inputs from a locally installed TLM JAR; no model asset is checked in. */
public final class LocalModelFixtures {
    public static void main(String[] args)throws Exception {
        Path output=Path.of(args[1]);Files.createDirectories(output);
        try(ZipFile jar=new ZipFile(args[0])) {
            String prefix="assets/touhou_little_maid/tlm_custom_pack/touhou_little_maid-1.0.0/assets/geckolib/";
            for(String name:List.of("zhiban_hanfu","winefox_hanfu")) {
                String geometry;
                try(var input=jar.getInputStream(jar.getEntry(prefix+"models/entity/"+name+".json"))) {
                    geometry=new String(input.readAllBytes(),StandardCharsets.UTF_8);
                }
                JsonObject animation;
                try(var input=jar.getInputStream(jar.getEntry(prefix+"animation/"+name+".animation.json"))) {
                    animation=JsonParser.parseString(new String(input.readAllBytes(),StandardCharsets.UTF_8)).getAsJsonObject().getAsJsonObject("animations");
                }
                Map<String,ScriptAnim> scripts=new LinkedHashMap<>();
                animation.entrySet().stream().filter(e->ScriptJson.isRuntimeRelevant(e.getKey()))
                        .forEach(e->scripts.put(e.getKey(),ScriptJson.fromBedrock(e.getKey(),e.getValue().getAsJsonObject())));
                YSMGeoModel model=Objects.requireNonNull(YSMGeoModel.parse(geometry));
                EFMeshJsonWriter.writeRuntimeJson(model,scripts,output.resolve(name+".runtime.json"),true);
                System.out.println("fixture="+name+" bones="+model.bonesByName.size()+" animations="+scripts.size());
            }
        }
    }
}
