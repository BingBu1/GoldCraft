/** Test boundary. Methods are replaced with the actual transformed AAA bytecode. */
public final class ActualDepthCalls {
    public static final class Target {
        public int frameBufferId,width,height;
        public Target(int fbo,int width,int height){this.frameBufferId=fbo;this.width=width;this.height=height;}
    }
    public static void copyDepthSafely(int fbo,int width,int height,Target target){throw new AssertionError("Real bytecode transplant required");}
    public static void copyDepthSafely(Target source,int fbo,int width,int height){throw new AssertionError("Real bytecode transplant required");}
    public static void copySafely(Target source,Target destination){throw new AssertionError("Real bytecode transplant required");}
}
