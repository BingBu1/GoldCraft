package dev.goldcraft.client;

import java.util.Map;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class EffectTexturesTest {
    private static final int TEXTURE0=33984,TEXTURE_2D=3553;
    @BeforeEach @AfterEach void reset() {
        EffectTextures.clear();
        for(int unit=0;unit<12;unit++){EffectTextures.active(TEXTURE0+unit);EffectTextures.bind(0);}
        EffectTextures.active(TEXTURE0);
    }
    private static int tracked() throws ReflectiveOperationException {
        var field=EffectTextures.class.getDeclaredField("revisions");field.setAccessible(true);
        return ((Map<?,?>)field.get(null)).size();
    }

    @Test void successiveGifUploadsInvalidateTheSameObjectWithoutRegisteringEachFrame() throws ReflectiveOperationException {
        EffectTextures.bind(71);long first=EffectTextures.revision(71);
        assertEquals(first,EffectTextures.revision(71));
        for(int i=0;i<4;i++) {
            EffectTextures.uploaded(TEXTURE_2D,0);long next=EffectTextures.revision(71);
            assertTrue(next>first);first=next;
        }
        assertEquals(1,tracked());
        EffectTextures.uploaded(TEXTURE_2D,1);EffectTextures.uploaded(34067,0);
        assertEquals(first,EffectTextures.revision(71),"mipmap and non-2D uploads do not replace exported base pixels");
    }

    @Test void deletionUnbindsEveryUnitAndReusingTheIdGetsANewRevision() {
        EffectTextures.bind(8);long original=EffectTextures.revision(8);
        EffectTextures.active(TEXTURE0+3);EffectTextures.bind(8);
        EffectTextures.deleted(8);long deleted=EffectTextures.revision(8);assertTrue(deleted>original);
        for(int unit:new int[]{0,3}) {
            EffectTextures.active(TEXTURE0+unit);EffectTextures.uploaded(TEXTURE_2D,0);
            assertEquals(deleted,EffectTextures.revision(8),"deleted texture must not remain bound");
        }
        EffectTextures.bind(8);EffectTextures.uploaded(TEXTURE_2D,0);
        assertTrue(EffectTextures.revision(8)>deleted,"a recycled GL name must invalidate old pixels");
    }

    @Test void activeUnitSelectsOnlyItsOwnBoundTexture() {
        EffectTextures.bind(11);long first=EffectTextures.revision(11);
        EffectTextures.active(TEXTURE0+2);EffectTextures.bind(22);long second=EffectTextures.revision(22);
        EffectTextures.uploaded(TEXTURE_2D,0);
        assertEquals(first,EffectTextures.revision(11));assertTrue(EffectTextures.revision(22)>second);
        second=EffectTextures.revision(22);EffectTextures.active(TEXTURE0);EffectTextures.uploaded(TEXTURE_2D,0);
        assertTrue(EffectTextures.revision(11)>first);assertEquals(second,EffectTextures.revision(22));
    }

    @Test void unrelatedUploadsAndDeletesCannotGrowTheExportedTextureCache() throws ReflectiveOperationException {
        long known=EffectTextures.revision(99);
        for(int texture=1000;texture<5000;texture++) {
            EffectTextures.bind(texture);EffectTextures.uploaded(TEXTURE_2D,0);EffectTextures.deleted(texture);
        }
        assertEquals(1,tracked());assertEquals(known,EffectTextures.revision(99));
    }

    @Test void invalidUnitsCannotOverwriteAValidBinding() {
        EffectTextures.bind(5);long first=EffectTextures.revision(5);
        for(int unit:new int[]{TEXTURE0-1,TEXTURE0+12,Integer.MAX_VALUE}) {
            EffectTextures.active(unit);EffectTextures.bind(6);EffectTextures.uploaded(TEXTURE_2D,0);
        }
        assertEquals(first,EffectTextures.revision(5));
        EffectTextures.active(TEXTURE0);EffectTextures.uploaded(TEXTURE_2D,0);
        assertTrue(EffectTextures.revision(5)>first);
    }

    @Test void clearingForANewSessionDiscardsTrackedContentWithoutReusingStamps() throws ReflectiveOperationException {
        EffectTextures.bind(4);long old=EffectTextures.revision(4);
        EffectTextures.clear();assertEquals(0,tracked());
        EffectTextures.uploaded(TEXTURE_2D,0);assertEquals(0,tracked());
        long fresh=EffectTextures.revision(4);assertTrue(fresh>old);
        EffectTextures.uploaded(TEXTURE_2D,0);assertTrue(EffectTextures.revision(4)>fresh);
    }
}
