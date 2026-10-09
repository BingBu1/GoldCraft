package dev.goldcraft.bridge;

import java.util.ArrayDeque;
import java.util.function.Supplier;

/** Ordered fragments. Only complete snapshots may supersede unstarted messages. */
public final class HostDeliveryQueue {
    private final ArrayDeque<Wire.Message> queue=new ArrayDeque<>();
    private int offset;
    public Wire.Message first(){return queue.peekFirst();}
    public int offset(){return offset;}
    public int size(){return queue.size();}
    private boolean unstarted(Wire.Message message){return offset==0||message!=queue.peekFirst();}
    public void snapshot(int type,byte[] bytes){
        if(bytes==null)return;
        queue.removeIf(m->m.type()==type&&unstarted(m));queue.addLast(new Wire.Message(type,bytes));
    }
    public void editSnapshot(byte[] bytes){
        if(bytes==null)return;
        queue.removeIf(m->isEdit(m)&&unstarted(m));queue.addLast(new Wire.Message(Wire.MAP_EDIT_SNAPSHOT,bytes));
    }
    public void editDelta(byte[] bytes,Supplier<byte[]> currentSnapshot){
        if(queue.stream().filter(HostDeliveryQueue::isEdit).count()>=256){editSnapshot(currentSnapshot.get());return;}
        queue.addLast(new Wire.Message(Wire.MAP_EDIT_DELTA,bytes));
    }
    private static boolean isEdit(Wire.Message m){return m.type()==Wire.MAP_EDIT_SNAPSHOT||m.type()==Wire.MAP_EDIT_DELTA;}
    public void advance(int bytes){
        if(queue.isEmpty()||bytes<=0||bytes>queue.peekFirst().payload().length-offset)throw new IllegalArgumentException("Host delivery progress");
        offset+=bytes;if(offset==queue.peekFirst().payload().length){queue.removeFirst();offset=0;}
    }
}
