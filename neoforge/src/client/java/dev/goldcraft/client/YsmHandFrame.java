package dev.goldcraft.client;

import com.google.gson.JsonObject;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import net.neoforged.fml.ModList;

/** Supplies YSM 2.6.5's per-frame hand reset for the separately exported HUD hand pass. */
final class YsmHandFrame {
    interface Hooks {
        boolean available();
        boolean rendered();
        void rendered(boolean value);
    }

    private static boolean resolved;
    private static YsmHandFrame installed;
    private final Hooks hooks;
    private boolean active,previousRendered;
    private long started,restored,failures;

    YsmHandFrame(Hooks hooks){this.hooks=hooks;}

    static boolean beginHosted(boolean hosted) {
        if(!hosted)return false;
        if(!resolved) {
            if(ModList.get().isLoaded("yes_steve_model"))installed=new YsmHandFrame(new InstalledHooks());
            resolved=true;
        }
        return installed!=null&&installed.begin();
    }

    static void endHosted(boolean owned){if(owned)installed.end();}

    static void diagnostics(JsonObject data) {
        if(installed==null)return;
        data.addProperty("ysmHandFrames",installed.started);
        data.addProperty("ysmHandRestored",installed.restored);
        data.addProperty("ysmHandFailures",installed.failures);
        data.addProperty("ysmHandActive",installed.active);
    }

    boolean begin() {
        if(active||!hooks.available())return false;
        previousRendered=hooks.rendered();
        active=true;
        try {
            // The skipped world stage normally resets this guard. The extra HUD
            // pass also needs its own reset if standalone rendering ran first.
            // One reset surrounds both hands; nested calls retain the guard.
            hooks.rendered(false);
            started++;
            return true;
        }catch(RuntimeException|Error failure) {
            failures++;
            try {end();}catch(RuntimeException|Error cleanup){failure.addSuppressed(cleanup);}
            throw failure;
        }
    }

    void end() {
        if(!active)return;
        try {
            // Do not leak this extra HUD pass into the ordinary Minecraft pass,
            // including when rendering throws after YSM consumes its guard.
            hooks.rendered(previousRendered);
            restored++;
        }catch(RuntimeException|Error failure) {
            failures++;
            throw failure;
        }finally {active=false;}
    }

    /** The private field and public availability API are verified against the installed 2.6.5 JAR. */
    private static final class InstalledHooks implements Hooks {
        private final MethodHandle available,get,set;
        InstalledHooks() {
            try {
                var lookup=MethodHandles.lookup();
                var api=Class.forName("com.elfmcys.yesstevemodel.YesSteveModel");
                var state=Class.forName("com.elfmcys.yesstevemodel.ooOOO000OO0Ooo0OoOooOOO0");
                available=lookup.findStatic(api,"isAvailable",MethodType.methodType(boolean.class));
                var flag=state.getDeclaredField("oOo0OO0O0o000OO0O000oo0o");
                flag.setAccessible(true);
                get=lookup.unreflectGetter(flag);
                set=lookup.unreflectSetter(flag);
            }catch(ReflectiveOperationException error) {
                throw new IllegalStateException("Installed YSM does not expose the verified 2.6.5 hand lifecycle",error);
            }
        }
        public boolean available(){try{return (boolean)available.invokeExact();}catch(Throwable error){throw failed(error);}}
        public boolean rendered(){try{return (boolean)get.invokeExact();}catch(Throwable error){throw failed(error);}}
        public void rendered(boolean value){try{set.invokeExact(value);}catch(Throwable error){throw failed(error);}}
        private static RuntimeException failed(Throwable error) {
            if(error instanceof Error fatal)throw fatal;
            return error instanceof RuntimeException runtime?runtime:new IllegalStateException("YSM hand lifecycle failed",error);
        }
    }
}
