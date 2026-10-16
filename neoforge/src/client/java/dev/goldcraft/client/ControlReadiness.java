package dev.goldcraft.client;

/** Admission reasons, independent of the short lease on native input samples. */
final class ControlReadiness {
    static final int LINK=1,GEOMETRY=2,ACTOR=4,DEAD=8,DIMENSION=16,FORM=32,
        LIFE=64,HOST_DEAD=128,SPECTATOR=256,WORLD=512,INPUT_AGE=1024,LEASE=2048,IDENTITY=4096;
    static int reasons(boolean link,boolean geometry,boolean actor,boolean alive,boolean dimension,
                       boolean form,int life,int flags,boolean world,boolean fresh,boolean changed) {
        return (link?0:LINK)|(geometry?0:GEOMETRY)|(actor?0:ACTOR)|(alive?0:DEAD)
            |(dimension?0:DIMENSION)|(form?0:FORM)|(life!=0?0:LIFE)|((flags&1)!=0?0:HOST_DEAD)
            |((flags&4)==0?0:SPECTATOR)|(world?0:WORLD)|(fresh?0:INPUT_AGE)
            |((flags&32)!=0?0:LEASE)|(changed?IDENTITY:0);
    }
    static boolean ready(int reasons) {
        // A stalled CS renderer must stop its old keys, not put a live MC
        // player into spectator mode. Authority still requires the server's
        // own current life, form, position proof and effect lease.
        return (reasons&~(INPUT_AGE|LEASE|IDENTITY))==0;
    }
    static boolean controlling(int reasons) {
        return ready(reasons)&&(reasons&(LEASE|IDENTITY))==0;
    }
    private ControlReadiness(){}
}
