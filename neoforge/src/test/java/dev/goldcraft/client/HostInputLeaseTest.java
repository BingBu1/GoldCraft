package dev.goldcraft.client;

import dev.goldcraft.bridge.BridgeLink;
import dev.goldcraft.bridge.Wire;
import java.lang.reflect.Field;
import java.net.InetAddress;
import java.net.ServerSocket;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

/** Uses production input expiry and readiness scheduling. No Minecraft window
 * or simulated server pose is used as evidence of gameplay acceptance. */
final class HostInputLeaseTest {
    private static Field field(Class<?> owner,String name) throws Exception {
        var field=owner.getDeclaredField(name);field.setAccessible(true);return field;
    }
    @Test void stoppedNativeFramesReleaseKeysWhileTheLiveMinecraftClientRenewsReadiness() throws Exception {
        try(var peer=new ServerSocket(0,1,InetAddress.getLoopbackAddress());
            var link=new BridgeLink(new BridgeLink.Config(peer.getLocalPort(),new byte[16],new byte[16],Wire.FABRIC_CLIENT,Wire.HOST_CLIENT))) {
            // As in BridgeLink's queue tests, stop/join its real worker before
            // setting connection state. This tests policy, not socket delivery.
            link.close();assertFalse(((Thread)field(BridgeLink.class,"worker").get(link)).isAlive());
            field(BridgeLink.class,"connected").setBoolean(link,true);
            var inputLink=field(HostInput.class,"link");var active=field(HostInput.class,"active");
            var lastInput=field(HostInput.class,"lastInput");
            Object previousLink=inputLink.get(null);int previousActive=active.getInt(null);long previousTime=lastInput.getLong(null);
            try {
                inputLink.set(null,link);active.setInt(null,1);
                long start=10_000_000_000L;
                var renewals=new ControlRequestSchedule();int sent=0;
                for(int tick=0;tick<=60;++tick) {
                    long age=tick*50_000_000L;
                    // The production input gate runs with a deterministic age;
                    // no blocking sleep or dependence on test machine FPS.
                    lastInput.setLong(null,System.nanoTime()-age);
                    boolean keys=HostInput.active();
                    if(age==0)assertTrue(keys);
                    if(age>=250_000_000L)assertFalse(keys,"expired held input at tick "+tick);
                    int reasons=ControlReadiness.reasons(true,true,true,true,true,true,7,33,true,age<500_000_000L,false);
                    boolean ready=ControlReadiness.ready(reasons);
                    assertTrue(ready);assertTrue(ControlReadiness.controlling(reasons));
                    if(renewals.due(start+age,ready,true)) {
                        renewals.sent(start+age,ready);++sent;
                    }
                }
                assertEquals(8,sent,"readiness remains renewable across a three-second CS stall");
                // A new fresh sample can resume input without a spectator /
                // teleport / movement-grant reacquisition cycle.
                lastInput.setLong(null,System.nanoTime());assertTrue(HostInput.active());
                active.setInt(null,0);assertFalse(HostInput.active());
                active.setInt(null,1);field(BridgeLink.class,"connected").setBoolean(link,false);
                assertFalse(HostInput.active());
                assertFalse(ControlReadiness.ready(ControlReadiness.LINK|ControlReadiness.INPUT_AGE));
                assertFalse(ControlReadiness.controlling(ControlReadiness.LEASE|ControlReadiness.INPUT_AGE));
                assertFalse(ControlReadiness.controlling(ControlReadiness.IDENTITY|ControlReadiness.INPUT_AGE));
            } finally {
                inputLink.set(null,previousLink);active.setInt(null,previousActive);lastInput.setLong(null,previousTime);
            }
        }
    }
}
