package dev.goldcraft.client;

import com.google.gson.JsonObject;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import net.neoforged.fml.ModList;

/** Replays YSM 2.6.5's world-animation lifecycle when hosted rendering skips LevelRenderer. */
final class YsmAnimationFrame {
    interface Hooks {
        boolean available();
        boolean world();
        void world(boolean value);
        void begin(float tickDelta);
        void end();
    }

    private static boolean resolved;
    private static YsmAnimationFrame installed;
    private final Hooks hooks;
    private boolean active,previousWorld;
    private long started,completed,failures;

    YsmAnimationFrame(Hooks hooks) {this.hooks=hooks;}

    static boolean beginHosted(boolean presentationOnly,float tickDelta) {
        if(!presentationOnly)return false;
        if(!resolved) {
            if(ModList.get().isLoaded("yes_steve_model"))installed=new YsmAnimationFrame(new InstalledHooks());
            resolved=true;
        }
        return installed!=null&&installed.begin(tickDelta);
    }

    static void endHosted(boolean owned) {if(owned)installed.end();}

    static void diagnostics(JsonObject data) {
        if(installed==null)return;
        data.addProperty("ysmAnimationFrames",installed.started);
        data.addProperty("ysmAnimationCompleted",installed.completed);
        data.addProperty("ysmAnimationFailures",installed.failures);
        data.addProperty("ysmAnimationActive",installed.active);
    }

    boolean begin(float tickDelta) {
        if(active||!hooks.available()||hooks.world())return false;
        previousWorld=hooks.world();
        active=true;
        try {
            hooks.world(true);
            hooks.begin(tickDelta);
            started++;
            return true;
        }catch(RuntimeException|Error failure) {
            failures++;
            // begin can queue some models before failing. Drain them before
            // restoring the world flag, just as after a failed entity draw.
            try {end();}catch(RuntimeException|Error cleanup){failure.addSuppressed(cleanup);}
            throw failure;
        }
    }

    void end() {
        if(!active)return;
        try {
            hooks.end();
            completed++;
        }catch(RuntimeException|Error failure) {
            failures++;
            throw failure;
        }finally {
            try {hooks.world(previousWorld);}finally {active=false;}
        }
    }

    /** Resolve the exact installed API once; successful frames allocate no invocation arrays. */
    private static final class InstalledHooks implements Hooks {
        private final MethodHandle available,worldGet,worldSet,begin,end;
        InstalledHooks() {
            try {
                var lookup=MethodHandles.lookup();
                var api=Class.forName("com.elfmcys.yesstevemodel.YesSteveModel");
                var state=Class.forName("com.elfmcys.yesstevemodel.Oo0OOO00o00oOOo0O0o000O0");
                var manager=Class.forName("com.elfmcys.yesstevemodel.O0OO0O0o00o0o00oOoO0o0oO");
                available=lookup.findStatic(api,"isAvailable",MethodType.methodType(boolean.class));
                // The public getter ORs shader compatibility flags into this
                // value. Only the raw field is safe to save and restore.
                var flag=state.getDeclaredField("OO000o0ooOooooOOOOO0Ooo0");
                flag.setAccessible(true);
                worldGet=lookup.unreflectGetter(flag);
                worldSet=lookup.unreflectSetter(flag);
                begin=lookup.findStatic(manager,"oOo0OO0O0o000OO0O000oo0o",MethodType.methodType(void.class,float.class));
                end=lookup.findStatic(manager,"oOo0OO0O0o000OO0O000oo0o",MethodType.methodType(void.class));
            }catch(ReflectiveOperationException error) {
                throw new IllegalStateException("Installed YSM does not expose the verified 2.6.5 animation lifecycle",error);
            }
        }
        public boolean available(){try{return (boolean)available.invokeExact();}catch(Throwable error){throw failed(error);}}
        public boolean world(){try{return (boolean)worldGet.invokeExact();}catch(Throwable error){throw failed(error);}}
        public void world(boolean value){try{worldSet.invokeExact(value);}catch(Throwable error){throw failed(error);}}
        public void begin(float delta){try{begin.invokeExact(delta);}catch(Throwable error){throw failed(error);}}
        public void end(){try{end.invokeExact();}catch(Throwable error){throw failed(error);}}
        private static RuntimeException failed(Throwable error) {
            if(error instanceof Error fatal)throw fatal;
            return error instanceof RuntimeException runtime?runtime:new IllegalStateException("YSM animation lifecycle failed",error);
        }
    }
}
