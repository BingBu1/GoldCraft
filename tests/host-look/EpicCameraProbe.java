package dev.goldcraft.client;
import net.minecraft.world.phys.Vec3;
import yesman.epicfight.api.client.camera.EpicFightCameraAPI;
import yesman.epicfight.api.client.input.InputManager;
import yesman.epicfight.config.ClientConfig;

public class EpicCameraProbe {
    static void near(double a,double b){if(Math.abs(a-b)>.001)throw new AssertionError(a+" != "+b);}
    static void check(boolean b){if(!b)throw new AssertionError();}
    public static void main(String[] args){
        var c=EpicFightCameraAPI.getInstance();c.minecraft.player.yaw=70;
        check(!HostEpicCamera.turn(6,3));near(c.minecraft.player.yaw,70);
        c.tps=true;check(HostEpicCamera.turn(6,3));near(c.cameraYRot,6);near(c.cameraXRot,3);near(c.minecraft.player.yaw,70);
        c.lockingOnTarget=true;c.cameraYRot=90;
        check(HostEpicCamera.turn(6,3));near(c.cameraYRot,90);near(c.minecraft.player.yaw,70);
        // Actual Epic + Minecraft movement methods must use body yaw while locked,
        // and free-camera yaw in TPS. Check W/A/S/D across a complete revolution.
        int checks=0;
        for(int angle=0;angle<360;angle+=15)for(int mode=0;mode<2;mode++)for(var input:new Vec3[]{new Vec3(0,0,1),new Vec3(0,0,-1),new Vec3(1,0,0),new Vec3(-1,0,0)}){
            c.lockingOnTarget=mode==1;c.minecraft.player.yaw=angle;c.cameraYRot=angle+73;
            var motion=c.getRelativeMove(input,1);double theta=Math.toRadians(mode==1?angle:angle+73);
            near(motion.x,input.x*Math.cos(theta)-input.z*Math.sin(theta));
            near(motion.z,input.z*Math.cos(theta)+input.x*Math.sin(theta));checks++;
        }
        c.lockingOnTarget=true;c.cameraXRot=0;c.cameraYRot=0;
        InputManager.free=true;check(HostEpicCamera.turn(12,-5));near(c.cameraYRot,12);near(c.cameraXRot,-5);
        InputManager.free=false;ClientConfig.lockOnSnapping=true;
        check(HostEpicCamera.turn(24,0));check(c.switches==1);near(c.cameraYRot,12.8);
        c.lockingOnTarget=false;c.tps=false;check(!HostEpicCamera.turn(2,1));near(c.cameraYRot,12.8);
        check(HostEpicCamera.error().isEmpty());
        System.out.println("PASS real pinned Epic turnCamera through candidate MethodHandles: FPS/TPS/lock/free shift/snap/unlock; "+checks+" real Epic + Minecraft W/A/S/D vectors. CPU adapters, not FML/gameplay.");
    }
}
