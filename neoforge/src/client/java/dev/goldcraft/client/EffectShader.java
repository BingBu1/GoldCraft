package dev.goldcraft.client;

/** Verified 1.21.1 core shader semantics; unknown programs are never silently substituted. */
record EffectShader(boolean texture,boolean light,float cutoff,int flags) {
    static EffectShader of(String name) {
        if(name.startsWith("minecraft:"))name=name.substring(10);
        return switch(name) {
            case "position_color_tex_lightmap" -> new EffectShader(true,false,.1f,16);
            case "position_color" -> new EffectShader(false,false,0,16|64);
            case "position_tex" -> new EffectShader(true,false,0,16|64);
            case "position_tex_color" -> new EffectShader(true,false,.1f,16);
            case "particle", "rendertype_text",
                 "rendertype_entity_translucent_cull", "rendertype_item_entity_translucent_cull" -> new EffectShader(true,true,.1f,0);
            case "rendertype_text_see_through" -> new EffectShader(true,false,.1f,16);
            case "rendertype_translucent", "rendertype_entity_solid", "rendertype_entity_no_outline" -> new EffectShader(true,true,0,0);
            case "rendertype_entity_translucent", "rendertype_entity_cutout", "rendertype_entity_cutout_no_cull" -> new EffectShader(true,true,.1f,32);
            case "rendertype_glint", "rendertype_glint_translucent", "rendertype_entity_glint",
                 "rendertype_entity_glint_direct", "rendertype_armor_entity_glint" -> new EffectShader(true,false,.1f,0);
            case "rendertype_text_background" -> new EffectShader(false,true,.1f,0);
            case "rendertype_text_background_see_through" -> new EffectShader(false,false,.1f,16);
            case "rendertype_lightning" -> new EffectShader(false,false,0,0);
            default -> null;
        };
    }
}
