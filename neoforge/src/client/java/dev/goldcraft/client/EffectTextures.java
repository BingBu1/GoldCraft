package dev.goldcraft.client;

import java.util.HashMap;
import java.util.Map;

/** Tracks only exported texture content. Upload/delete hooks also cover animated Mod textures. */
public final class EffectTextures {
    private static final int[] bound=new int[12];
    private static final Map<Integer,Long> revisions=new HashMap<>();
    private static long sequence,invalidations;
    private static int unit;
    private EffectTextures() {}
    public static void active(int glUnit){unit=glUnit-33984;}
    public static void bind(int texture){if(unit>=0&&unit<bound.length)bound[unit]=texture;}
    public static void uploaded(int target,int level){if(target==3553&&level==0&&unit>=0&&unit<bound.length)changed(bound[unit]);}
    public static void deleted(int texture){changed(texture);for(int i=0;i<bound.length;i++)if(bound[i]==texture)bound[i]=0;}
    private static void changed(int id){if(revisions.containsKey(id)){revisions.put(id,++sequence);invalidations++;}}
    static long invalidations(){return invalidations;}
    static long revision(int id){return revisions.computeIfAbsent(id,ignored->++sequence);}
    static void clear(){revisions.clear();}
}
