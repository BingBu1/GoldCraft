    // Only Minecraft world/rendering and the transport endpoint are adapted.
    // Exporter fields, section traversal, lifecycle and send methods are extracted unchanged.
    static final class BridgeLink {
        final List<Wire.Message> messages=new ArrayList<>();
        int rejectType=-1;
        boolean connected(){return true;}
        boolean send(int type,byte[] payload){
            if(type==rejectType)return false;
            messages.add(new Wire.Message(type,payload));return true;
        }
    }
    static class Resettable {void reset(){}}
    static final class EntityExporter extends Resettable {EntityExporter(BridgeLink link){}}
    static final class ParticleExporter extends Resettable {ParticleExporter(BridgeLink link){}}
    static final class BlockFeedbackExporter {BlockFeedbackExporter(BridgeLink link){}}
    static final class AtlasAnimationState {
        void reset(int width,int height){} boolean dirty(){return false;}
        byte[] snapshot(long epoch,long generation){return new byte[0];} void sent(){}
    }
    static final class SpriteAtlasTexture {
        static final String BLOCK_ATLAS_TEXTURE="blocks";
        String getId(){return BLOCK_ATLAS_TEXTURE;}
    }
    static final class GoldCraft {
        static final GoldCraft LOGGER=new GoldCraft();
        void warn(String message,String error){throw new AssertionError(message+error);}
    }
    static final class MinecraftClient {
        static MinecraftClient instance;
        ClientWorld world;final Player player=new Player();
        static MinecraftClient getInstance(){return instance;}
        Renderer getBlockRenderManager(){return new Renderer();}
    }
    static final class Player {
        BlockPos position=new BlockPos(0,64,0);
        BlockPos getBlockPos(){return position;}
    }
    static class BlockPos {
        int x,y,z;
        BlockPos(int x,int y,int z){this.x=x;this.y=y;this.z=z;}
        int getX(){return x;} int getY(){return y;} int getZ(){return z;}
        static final class Mutable extends BlockPos {
            Mutable(){super(0,0,0);} void set(int x,int y,int z){this.x=x;this.y=y;this.z=z;}
        }
    }
    interface BlockLookup {State get(int x,int y,int z);}
    static final class ClientWorld {
        BlockLookup lookup=(x,y,z)->State.AIR;
        boolean loaded=true;
        boolean isChunkLoaded(int x,int z){return loaded;}
        Chunk getChunk(int x,int z){return new Chunk();}
        int sectionCoordToIndex(int y){return y+4;}
        int getBottomY(){return -64;} int getTopY(){return 320;}
        State getBlockState(BlockPos position){return lookup.get(position.x,position.y,position.z);}
    }
    static final class Chunk {
        Object[] getSectionArray(){return new Object[24];}
        Chunk getSection(int index){return this;} boolean isEmpty(){return false;}
    }
    static final class State {
        static final State AIR=new State(0,"air"),TORCH=new State(14,"torch"),LAMP=new State(15,"sea_lantern");
        final int luminance;final String id;
        State(int luminance,String id){this.luminance=luminance;this.id=id;}
        boolean isAir(){return id.equals("air");} int getLuminance(){return luminance;}
        Object getBlock(){return id;} BlockRenderType getRenderType(){return BlockRenderType.MODEL;}
        Fluid getFluidState(){return new Fluid();}
    }
    enum BlockRenderType {MODEL}
    static final class Fluid {boolean isEmpty(){return true;}}
    static final class MatrixStack {void push(){}void pop(){}void translate(int x,int y,int z){}}
    static final class Random {static Random create(long seed){return new Random();}}
    static final class RenderLayer {static RenderLayer getTranslucent(){return null;}}
    static final class RenderLayers {
        static RenderLayer getBlockLayer(State state){return null;}
        static RenderLayer getFluidLayer(Fluid fluid){return null;}
    }
    static final class Collector {
        int checksum;
        void offset(int x,int y,int z){}
        byte[] finish(){return new Wire.Writer().i32(checksum).bytes(new byte[20]).toByteArray();}
    }
    static final class Renderer {
        void renderBlock(State state,BlockPos pos,ClientWorld world,MatrixStack matrices,Collector collector,boolean cull,Random random){
            collector.checksum=31*collector.checksum+pos.x*17+pos.y*13+pos.z*7+state.luminance;
        }
        void renderFluid(BlockPos pos,ClientWorld world,Collector collector,State state,Fluid fluid){}
    }
    private boolean exportAtlas(MinecraftClient client){return true;}

    // Test entrypoints do not change any extracted production method body.
    static WorldExporter fixture(ClientWorld world) {
        MinecraftClient.instance=new MinecraftClient();MinecraftClient.instance.world=world;
        WorldExporter exporter=new WorldExporter(new BridgeLink());exporter.bind(42);
        exporter.sourceWorld=world;exporter.atlasSent=true;return exporter;
    }
    void export(int x,int y,int z){exportSection(MinecraftClient.getInstance(),new Section(x,y,z));}
    void refreshNow(){refresh(MinecraftClient.getInstance());}
    void renderNow(){renderFrameInternal();}
    byte[] lights(){sendLights();return link.messages.getLast().payload();}
    BridgeLink endpoint(){return link;}
    boolean exported(int x,int y,int z){return sent.containsKey(new Section(x,y,z));}
