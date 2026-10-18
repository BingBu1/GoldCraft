package dev.goldcraft.bridge;

/** Rewards above the healing cap remain HP, not a new healing cap. */
final class HealthPrediction {
    private HealthPrediction(){}
    static float apply(float health,float delta,float maximum){
        return delta>0?Math.max(health,Math.min(health+delta,maximum)):Math.max(0,health+delta);
    }
}
