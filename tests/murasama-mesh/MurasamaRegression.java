package probe;
import com.google.gson.*;
import com.hm.efn.client.renderer.RenderMurasama;
import java.nio.file.*;
import java.util.*;
import java.lang.reflect.*;
import com.mojang.blaze3d.vertex.PoseStack;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.item.ItemStack;
import yesman.epicfight.api.client.model.*;
import yesman.epicfight.api.utils.math.OpenMatrix4f;
import yesman.epicfight.world.capabilities.entitypatch.*;
import yesman.epicfight.world.capabilities.entitypatch.player.*;
public final class MurasamaRegression {
 static int checks;static Method original;
 static void check(boolean v,String reason){checks++;if(!v)throw new AssertionError(reason);}
 static Object field(Object r,String n)throws Exception{var f=r.getClass().getDeclaredField(n);f.setAccessible(true);return f.get(r);}
 record Outcome(List<String> draws,List<String> gets,int depth,int pushes,int pops,int poses,Throwable failure){}
 static Outcome render(RenderMurasama renderer,boolean old,InteractionHand hand,int sheath,boolean player,boolean nil)throws Exception {
  Meshes.gets.clear();SkinnedMesh.draws.clear();var pose=new PoseStack();LivingEntityPatch<?> patch=nil?null:player?new PlayerPatch<>():new LivingEntityPatch<>();Throwable failure=null;
  Object[] args={new ItemStack(sheath),patch,hand,new OpenMatrix4f[0],null,pose,123,.5f};
  try{if(old)original.invoke(renderer,args);else renderer.renderItemInHand((ItemStack)args[0],patch,hand,(OpenMatrix4f[])args[3],null,pose,123,.5f);}
  catch(InvocationTargetException e){failure=e.getCause();}catch(RuntimeException e){failure=e;}
  return new Outcome(List.copyOf(SkinnedMesh.draws),List.copyOf(Meshes.gets),pose.depth,pose.pushes,pose.pops,patch==null?0:patch.arm.poses,failure);
 }
 public static void main(String[] args)throws Exception {
  original=RenderMurasama.class.getMethod("renderOriginal",ItemStack.class,LivingEntityPatch.class,InteractionHand.class,OpenMatrix4f[].class,net.minecraft.client.renderer.MultiBufferSource.class,PoseStack.class,int.class,float.class);
  var json=JsonParser.parseString(Files.readString(Path.of(args[0]))).getAsJsonObject();
  for(int reload=0;reload<2;reload++) {
   var r=new RenderMurasama(json);
   check(field(r,"mesh1_off")==null&&field(r,"mesh2_off")==null,"actual constructor preserves missing optional keys on reload");
   check(field(r,"mesh1_main")!=null&&field(r,"mesh2_main")!=null,"actual main asset accessors registered");
   for(boolean player:new boolean[]{true,false})for(int sheath:new int[]{0,1}) {
    var old=render(r,true,InteractionHand.MAIN_HAND,sheath,player,false);var fixed=render(r,false,InteractionHand.MAIN_HAND,sheath,player,false);
    check(old.equals(fixed),"all mainhand draw arguments and armature/pose behavior unchanged");check(fixed.draws.size()==2&&fixed.gets.size()==1,"base+emissive exact accessor once");
    check(fixed.gets.getFirst().endsWith(player&&sheath==1?"hf_murasama_sheath":"hf_murasama"),"real sheath/player selection");
    var bad=render(r,true,InteractionHand.OFF_HAND,sheath,player,false);check(bad.failure instanceof NullPointerException,"exact original missing offhand NPE");check(bad.depth==1&&bad.pops==0,"original leaks pushed pose before NPE");
    var ok=render(r,false,InteractionHand.OFF_HAND,sheath,player,false);check(ok.failure==null&&ok.draws.isEmpty()&&ok.gets.isEmpty(),"omitted custom mesh follows existing no draw branch");
    check(ok.depth==0&&ok.pushes==1&&ok.pops==1&&ok.poses==1,"original armature update and balanced pose retained");
   }
   check(render(r,false,InteractionHand.OFF_HAND,0,true,true).equals(render(r,true,InteractionHand.OFF_HAND,0,true,true)),"null patch original no-op preserved");
  }
  var custom=json.deepCopy();custom.addProperty("mesh1_off","fixture:weapon/distinct_off_unsheathed");custom.addProperty("mesh2_off","fixture:weapon/distinct_off_sheathed");var r=new RenderMurasama(custom);
  for(int sheath:new int[]{0,1}){var a=render(r,true,InteractionHand.OFF_HAND,sheath,true,false);var b=render(r,false,InteractionHand.OFF_HAND,sheath,true,false);check(a.equals(b),"explicit resource-pack offhand draw unchanged");check(b.gets.getFirst().startsWith("fixture:"),"configured offhand used, never main model");}
  Meshes.returnNullId="fixture:weapon/distinct_off_unsheathed";check(render(r,true,InteractionHand.OFF_HAND,0,true,false).equals(render(r,false,InteractionHand.OFF_HAND,0,true,false)),"non-null accessor returning null retains original semantics");Meshes.returnNullId=null;
  Meshes.failId="fixture:weapon/distinct_off_unsheathed";var a=render(r,true,InteractionHand.OFF_HAND,0,true,false);var b=render(r,false,InteractionHand.OFF_HAND,0,true,false);
  check(a.failure instanceof IllegalStateException&&b.failure instanceof IllegalStateException&&a.failure.getMessage().equals(b.failure.getMessage()),"real configured asset failure remains visible");check(a.gets.equals(b.gets)&&b.gets.size()==1,"failing accessor executes exactly once");
  check(a.depth==b.depth,"no broad cleanup/exception catch added");Meshes.failId=null;
  System.out.println("PASS exact original constructor/render + Sponge-transformed render checks="+checks+"; stock main/off, sheath0/1, player/nonplayer, reload, null patch, resource override, accessor-null result and error propagation");
 }
}
