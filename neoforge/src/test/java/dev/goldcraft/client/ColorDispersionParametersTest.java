package dev.goldcraft.client;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class ColorDispersionParametersTest {
    private static float[] parameters(int age,int lifetime,boolean post) {
        var result=new float[8];ColorDispersionParameters.write(result,age,lifetime,post,.25f,.75f);return result;
    }
    @Test void levelsDecayToIdentityAndCenterRemainsTheActualUniform() {
        var start=parameters(0,40,false);assertArrayEquals(new float[]{1.15f,1.075f,1,.25f,.75f,.5f,.5f,1.2f},start);
        var middle=parameters(10,40,false);assertEquals(1.1299038f,middle[0]);assertEquals(1.0649519f,middle[1]);
        for(int age:new int[]{40,41,80}) {
            var expired=parameters(age,40,false);assertArrayEquals(new float[]{1,1,1,.25f,.75f,.5f,.5f,1.2f},expired);
        }
    }
    @Test void postChangesColorOnlyAtActualEightTickBoundariesAndEndsInGrayscale() {
        int[] ages={0,7,8,15,16,23,24,39,40};
        float[][] expected={{.7f,.7f,1.1f},{.7f,.7f,1.1f},{.5f,.5f,1.2f},{.5f,.5f,1.2f},
            {.8f,.8f,1.2f},{.8f,.8f,1.2f},{0,0,0},{0,0,0},{0,0,0}};
        for(int i=0;i<ages.length;i++) {
            var actual=parameters(ages[i],40,true);
            assertArrayEquals(expected[i],java.util.Arrays.copyOfRange(actual,5,8),"age="+ages[i]);
            assertArrayEquals(new float[]{.5f,.5f,1.2f},java.util.Arrays.copyOfRange(parameters(ages[i],40,false),5,8));
        }
    }
    @Test void invalidLifetimesCannotCreateDivideByZeroUniforms() {
        for(int lifetime:new int[]{0,-1})assertThrows(IllegalArgumentException.class,()->parameters(0,lifetime,true));
        assertThrows(IllegalArgumentException.class,()->parameters(-1,40,true));
    }
}
