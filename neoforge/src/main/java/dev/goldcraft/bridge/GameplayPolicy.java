package dev.goldcraft.bridge;

/** Authenticated server policy; block clear commands remain cumulative across reconnects. */
public record GameplayPolicy(long world,long revision,long clearRevision,boolean clearBlocks,boolean dropItems) {
    public static GameplayPolicy defaults(){return new GameplayPolicy(0,0,0,false,true);}
    public static GameplayPolicy decode(byte[] payload){
        var r=new Wire.Reader(payload);
        long world=r.i64(),revision=r.i64(),clear=r.i64();int blocks=r.i32(),drops=r.i32();r.finish();
        if(world==0||revision==0||Long.compareUnsigned(clear,revision)>0||(blocks&~1)!=0||(drops&~1)!=0)
            throw new IllegalArgumentException("Gameplay policy bounds");
        return new GameplayPolicy(world,revision,clear,blocks==1,drops==1);
    }
    public boolean supersedes(GameplayPolicy previous,long currentWorld){
        return world==currentWorld&&currentWorld!=0&&
            (previous.world()!=world||(Long.compareUnsigned(revision,previous.revision())>0&&
             Long.compareUnsigned(clearRevision,previous.clearRevision())>=0));
    }
}
