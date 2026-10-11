package dev.goldcraft.client;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

/** Expectations checked against Minecraft 1.21.1's actual core vertex/fragment shaders. */
class EffectShaderTest {
    private static final int BEFORE_MOD=16,TEXTURE_ONLY=32,ZERO_ALPHA=64;
    private static void policy(String name,boolean texture,boolean light,float cutoff,int flags) {
        assertEquals(new EffectShader(texture,light,cutoff,flags),EffectShader.of(name),name);
    }

    @Test void chatBubbleShaderDoesNotUseItsUv2Attribute() {
        policy("position_color_tex_lightmap",true,false,.1f,BEFORE_MOD);
    }

    @Test void guiStyleProgramsPreserveCutoffOrderAndZeroAlphaSemantics() {
        policy("position_color",false,false,0,BEFORE_MOD|ZERO_ALPHA);
        policy("position_tex",true,false,0,BEFORE_MOD|ZERO_ALPHA);
        policy("position_tex_color",true,false,.1f,BEFORE_MOD);
    }

    @Test void seeThroughTextHasNeitherUv2NorLightmapAndCutsOffBeforeModulation() {
        policy("rendertype_text_see_through",true,false,.1f,BEFORE_MOD);
        policy("rendertype_text_background_see_through",false,false,.1f,BEFORE_MOD);
    }

    @Test void worldShadersKeepTheirDistinctDiscardRules() {
        policy("particle",true,true,.1f,0);
        policy("rendertype_text",true,true,.1f,0);
        policy("rendertype_text_background",false,true,.1f,0);
        policy("rendertype_entity_translucent_cull",true,true,.1f,0);
        policy("rendertype_entity_translucent",true,true,.1f,TEXTURE_ONLY);
        policy("rendertype_translucent",true,true,0,0);
        policy("rendertype_lightning",false,false,0,0);
    }

    @Test void vanillaGlintProgramsUseTheCapturedDepthAndBlendState() {
        for(String name:new String[]{"rendertype_glint","rendertype_glint_translucent",
            "rendertype_entity_glint","rendertype_entity_glint_direct","rendertype_armor_entity_glint"})
            policy(name,true,false,.1f,0);
    }

    @Test void onlyVerifiedCoreNamesAreAcceptedAndMinecraftPrefixIsEquivalent() {
        assertEquals(EffectShader.of("particle"),EffectShader.of("minecraft:particle"));
        assertNull(EffectShader.of("other_mod:particle"));
        assertNull(EffectShader.of("position_color_tex"),"this name is absent from the 1.21.1 core shader archive");
        assertNull(EffectShader.of("custom_unverified"));
    }
}
