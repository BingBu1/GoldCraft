package dev.goldcraft.client;

import org.joml.Matrix4f;
import org.joml.Quaternionf;
import org.joml.Vector3f;
import org.joml.Vector4f;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class YsmCaptureProjectionTest {
    @Test void worldVerticesMatchIndependentLookAtAcrossCameraAngles() {
        var uiProjection=new Matrix4f().ortho(0,640,360,0,1000,21000);
        try {
            for(int i=0;i<512;i++) {
                float x=(i%13-6)*8,y=64+(i%7),z=(i%19-9)*4;
                var camera=new Quaternionf().rotationYXZ(i*.031f,(i%19-9)*.07f,0);
                var eye=new Vector3f(x,y,z);
                var forward=new Vector3f(0,0,-1).rotate(camera);
                var up=new Vector3f(0,1,0).rotate(camera);
                var projection=new Matrix4f().perspective((float)Math.toRadians(70),16f/9f,.05f,512);
                var reference=new Matrix4f(projection).lookAt(eye,new Vector3f(eye).add(forward),up);
                YsmCaptureProjection.begin(projection,camera,x,y,z);
                var passed=new Matrix4f(uiProjection);
                assertSame(passed,YsmCaptureProjection.forNativeDraw(passed));
                for(int v=0;v<8;v++) {
                    var vertex=new Vector3f((v&1)==0?-.25f:.25f,(v&2)==0?-.25f:.25f,
                        -3+((v&4)==0?-.25f:.25f)).rotate(camera).add(eye);
                    var expected=reference.transform(new Vector4f(vertex,1));
                    assertTrue(expected.distance(passed.transform(new Vector4f(vertex,1)))<.002f);
                    assertTrue(expected.distance(uiProjection.transform(new Vector4f(vertex,1)))>.5f,
                        "the retained UI projection must reproduce the wrong camera basis");
                }
            }
        }finally {YsmCaptureProjection.end();}
    }

    @Test void cameraAndProjectionRemainUnchangedAndGuiIsPassedThrough() {
        var projection=new Matrix4f().perspective(1.2f,16f/9f,.05f,512);
        var rotation=new Quaternionf().rotationYXZ(.3f,.8f,0);
        var beforeProjection=new Matrix4f(projection);var beforeRotation=new Quaternionf(rotation);
        var gui=new Matrix4f().ortho(0,640,360,0,1000,21000);var beforeGui=new Matrix4f(gui);
        assertSame(gui,YsmCaptureProjection.forNativeDraw(gui));assertEquals(beforeGui,gui);
        try {
            YsmCaptureProjection.begin(projection,rotation,48,65,12);
            YsmCaptureProjection.forNativeDraw(new Matrix4f());
            assertEquals(beforeProjection,projection);assertEquals(beforeRotation,rotation);
        }finally {YsmCaptureProjection.end();}
        assertSame(gui,YsmCaptureProjection.forNativeDraw(gui));assertEquals(beforeGui,gui);
    }

    @Test void exporterCleanupClearsProjectionEvenWithoutAnimationOwnership() {
        YsmCaptureProjection.begin(new Matrix4f().perspective(1.2f,1,.05f,128),new Quaternionf(),24,65,8);
        // A model can be unavailable/already in an animation frame; the export
        // finally block still calls endHosted(false).
        YsmAnimationFrame.endHosted(false);
        var gui=new Matrix4f().translation(1,2,3);var expected=new Matrix4f(gui);
        assertSame(gui,YsmCaptureProjection.forNativeDraw(gui));assertEquals(expected,gui);
    }
}
