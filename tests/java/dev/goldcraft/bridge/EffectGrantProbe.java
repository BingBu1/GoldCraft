package dev.goldcraft.bridge;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Arrays;
import java.util.HexFormat;

/** Cross-language schema oracle and malformed/unsigned boundary checks. */
public final class EffectGrantProbe {
    private static int checks;
    private static void check(boolean condition){checks++;if(!condition)throw new AssertionError("check "+checks);}
    private static final byte[] GOLDEN=HexFormat.of().parseHex("010000000100000002000000000000000300000000000000010000000000000001000000040000000000000005000000060000000102030405060708090a0b0c0d0e0f100800000000000000090000000000000007000000000000008096980000000000e803000000000000d6060000000000000100000000000042000080c20000c04200008041000080c20000a042020000000000000005000000000000000000803f");
    private static byte[] altered(int offset,long value,int width){
        byte[] b=GOLDEN.clone();for(int i=0;i<width;i++)b[offset+i]=(byte)(value>>>(8*i));return b;
    }
    private static void rejects(byte[] bytes){
        boolean bad=false;try{HostEffectGrant.decode(bytes);}catch(IllegalArgumentException expected){bad=true;}
        check(bad);
    }
    public static void main(String[] args){
        var g=HostEffectGrant.decode(GOLDEN);
        check(g.operation()==HostEffectGrant.OPEN&&g.serviceGeneration()==2&&g.nativeConnection()==3);
        check(g.address().target().world()==1&&g.address().target().key()==4&&g.address().target().serial()==5&&g.address().target().life()==6);
        check(g.stream()==8&&g.publication()==9&&g.snapshot().revision()==7);
        check(g.frequency()==10000000&&g.lease().issuedAtMillis()==1000&&g.lease().deadlineMillis()==1750&&g.bootstrap());
        check(g.nativeFeet().equals(new HostEffectGrant.Point(32,-64,96)));
        check(g.nativeAnchorFeet().equals(new HostEffectGrant.Point(16,-64,80)));
        check(g.snapshot().movement().equals(new HostEffects.MovementState(2,0,5,0,1)));
        check(Arrays.equals(GOLDEN,HostEffectGrant.encode(g)));
        for(int n=0;n<GOLDEN.length;n++)rejects(Arrays.copyOf(GOLDEN,n));
        rejects(Arrays.copyOf(GOLDEN,GOLDEN.length+1));
        for(int offset:new int[]{0,4,8,16,24,36,44,48,68,76,84,92})rejects(altered(offset,0,4));
        rejects(altered(32,2,4));rejects(altered(36,65,8));rejects(altered(116,2,4));
        rejects(altered(108,1000,8));rejects(altered(108,61001,8));rejects(altered(92,-1,8));
        rejects(altered(120,0x7fc00000,4));rejects(altered(120,Float.floatToIntBits(16385),4));
        rejects(altered(144,16,4));rejects(altered(148,1,4));rejects(altered(152,8,4));
        rejects(altered(156,Float.floatToIntBits(1),4));rejects(altered(160,0,4));
        // Identity counters keep their unsigned bit patterns even though the current issuer exhausts earlier.
        var high=HostEffectGrant.decode(altered(68,Long.MIN_VALUE,8));check(high.stream()==Long.MIN_VALUE);
        var highPub=HostEffectGrant.decode(altered(76,-1,8));check(highPub.publication()==-1);
        byte[] prefix=HostEffectGrant.posePrefix(g).toByteArray();check(prefix.length==36);
        var p=ByteBuffer.wrap(prefix).order(ByteOrder.LITTLE_ENDIAN);
        check(p.getInt()==1&&p.getLong()==2&&p.getLong()==3&&p.getLong()==8&&p.getLong()==9);
        var zero=ByteBuffer.wrap(HostEffectGrant.posePrefix(null).toByteArray()).order(ByteOrder.LITTLE_ENDIAN);
        check(zero.getInt()==1&&zero.getLong()==0&&zero.getLong()==0&&zero.getLong()==0&&zero.getLong()==0);
        byte[] reopen=HostEffectGrant.reopen(g);check(reopen.length==64);
        var r=new Wire.Reader(reopen);check(r.i32()==1&&r.i64()==1&&r.i32()==4&&r.i32()==5&&r.i32()==6);
        check(Arrays.equals(r.bytes(16),Wire.uuid(g.minecraftUuid())));check(r.i64()==2&&r.i64()==3&&r.i64()==8);r.finish();
        System.out.println("effect grant Java codec checks: "+checks+", no physical execution");
    }
}
