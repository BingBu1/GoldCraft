package dev.goldcraft.client;

import java.util.concurrent.atomic.AtomicReference;
import org.joml.Matrix4f;
import org.joml.Matrix4fStack;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class ModelViewScopeTest {
    @Test void repeatedModExceptionsPreserveCallerStackAndDoNotExhaustIt() {
        var outer=new Matrix4fStack(16);
        outer.translation(1,2,3);
        var base=new Matrix4f(outer);
        outer.pushMatrix().rotateY(0.4f);
        var top=new Matrix4f(outer);
        var current=new AtomicReference<>(outer);
        var scratch=new Matrix4fStack(16);
        for(int frame=0;frame<128;frame++) {
            assertThrows(IllegalStateException.class,()->{
                try(var scope=new ModelViewScope(current.get(),scratch,current::set)) {
                    assertSame(scratch,current.get());
                    assertEquals(top,new Matrix4f(current.get()));
                    // Reproduce a Mod leaving all of its pushes behind when rendering throws.
                    for(int push=0;push<16;push++)current.get().pushMatrix().translation(push,0,0);
                }
            });
            assertSame(outer,current.get());
            assertEquals(top,new Matrix4f(outer));
        }
        outer.popMatrix();
        assertEquals(base,new Matrix4f(outer));
        assertThrows(IllegalStateException.class,outer::popMatrix);
    }

    @Test void nestedScopesRestoreEachOwnersOriginalStack() {
        var outer=new Matrix4fStack(16);
        var current=new AtomicReference<>(outer);
        var first=new Matrix4fStack(16);
        var second=new Matrix4fStack(16);
        try(var scope=new ModelViewScope(current.get(),first,current::set)) {
            first.pushMatrix().translation(4,5,6);
            try(var inner=new ModelViewScope(current.get(),second,current::set)) {
                second.pushMatrix().scale(3);
            }
            assertSame(first,current.get());
            assertEquals(new Matrix4f().translation(4,5,6),new Matrix4f(first));
            assertThrows(IllegalArgumentException.class,()->new ModelViewScope(first,first,current::set));
        }
        assertSame(outer,current.get());
        assertEquals(new Matrix4f(),new Matrix4f(outer));
    }
}
