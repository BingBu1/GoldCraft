package dev.goldcraft.net;

import dev.goldcraft.bridge.Wire;

/** At most one bounded transfer is allocated per player. Ordered NeoForge networking supplies fragments. */
public final class HostAssembler {
    private byte[] pending;
    private int kind,position;
    public void clear(){pending=null;position=kind=0;}
    public Wire.Message accept(HostPayload part) {
        if(part.offset()==0){pending=new byte[part.total()];position=0;kind=part.type();}
        if(pending==null||part.offset()!=position||part.type()!=kind||part.total()!=pending.length) {clear();throw new IllegalArgumentException("Host fragment sequence");}
        System.arraycopy(part.data(),0,pending,position,part.data().length);position+=part.data().length;
        if(position!=pending.length)return null;
        Wire.Message message=new Wire.Message(kind,pending);clear();return message;
    }
}
