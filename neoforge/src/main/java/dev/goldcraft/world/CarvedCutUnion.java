package dev.goldcraft.world;

import java.util.*;
import static dev.goldcraft.world.CarvedVolume.*;

/** Merge only exactly matching cross sections. Never fill a missing voxel or
 * change the authoritative edit list; this is a collision preparation input. */
final class CarvedCutUnion {
    private CarvedCutUnion() {}
    static List<Box> compact(List<Box> input) {
        var boxes=new ArrayList<>(input);
        int previous,passes=0;
        do {
            previous=boxes.size();
            for(int axis=0;axis<3;axis++) {
                final int a=axis,u=(axis+1)%3,v=(axis+2)%3;
                boxes.sort(Comparator.comparingDouble((Box b)->b.min().at(u))
                    .thenComparingDouble(b->b.max().at(u)).thenComparingDouble(b->b.min().at(v))
                    .thenComparingDouble(b->b.max().at(v)).thenComparingDouble(b->b.min().at(a))
                    .thenComparingDouble(b->b.max().at(a)));
                var next=new ArrayList<Box>();
                for(var b:boxes) {
                    if(!next.isEmpty()) {
                        var last=next.getLast();
                        if(last.min().at(u)==b.min().at(u)&&last.max().at(u)==b.max().at(u)
                            &&last.min().at(v)==b.min().at(v)&&last.max().at(v)==b.max().at(v)
                            &&b.min().at(a)<=last.max().at(a)) {
                            next.set(next.size()-1,new Box(last.min(),last.max().at(a,Math.max(last.max().at(a),b.max().at(a)))));
                            continue;
                        }
                    }
                    next.add(b);
                }
                boxes=next;
            }
        // Incomplete compaction is still the exact same union. Bound hostile
        // fragmented layouts instead of iterating once per possible merge.
        } while(boxes.size()<previous&&++passes<8);
        return List.copyOf(boxes);
    }
}
