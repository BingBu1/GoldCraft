package dev.goldcraft.bridge;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class GameplayPolicyTest {
    private byte[] packet(long world,long revision,long clear,int blocks,int drops){
        return new Wire.Writer().i64(world).i64(revision).i64(clear).i32(blocks).i32(drops).toByteArray();
    }
    @Test void policyIsScopedAndMonotonic(){
        var first=GameplayPolicy.decode(packet(19,3,1,1,0));
        assertTrue(first.supersedes(GameplayPolicy.defaults(),19));
        assertFalse(first.dropItems());assertTrue(first.clearBlocks());
        assertFalse(first.supersedes(first,19));
        assertFalse(first.supersedes(GameplayPolicy.defaults(),20));
        assertFalse(GameplayPolicy.decode(packet(19,4,0,0,1)).supersedes(first,19));
        assertTrue(GameplayPolicy.decode(packet(19,4,2,0,1)).supersedes(first,19));
        assertTrue(GameplayPolicy.defaults().dropItems());
    }
    @Test void invalidPoliciesCannotChangeRules(){
        for(byte[] value:new byte[][]{packet(0,1,0,0,1),packet(1,0,0,0,1),packet(1,1,2,1,1),
                packet(1,1,0,2,1),packet(1,1,0,0,-1),new byte[31],new byte[33]})
            assertThrows(IllegalArgumentException.class,()->GameplayPolicy.decode(value));
    }
}
