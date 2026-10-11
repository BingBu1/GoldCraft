package dev.goldcraft.client;

/** Attempt every restoration step without replacing the original render failure. */
final class CaptureCleanup {
    private Throwable failure;
    CaptureCleanup() {}
    CaptureCleanup(Throwable failure) {this.failure=failure;}
    void run(Runnable action) {
        try {action.run();}
        catch(RuntimeException|Error error) {
            if(failure==null)failure=error;
            else if(error!=failure)failure.addSuppressed(error);
        }
    }
    void rethrow() {
        if(failure instanceof Error error)throw error;
        if(failure instanceof RuntimeException error)throw error;
    }
}
