#include "async_diagnostics.hpp"

namespace { goldcraft::diagnostics::Dispatcher dispatcher; }
extern "C" {
bool AsyncStart(const char* log, const char* status) {
    return dispatcher.start({log, status, false});
}
void AsyncSubmit() {
    dispatcher.log("DLL worker log\n");
    dispatcher.snapshot("{\"dll\":true}");
}
void AsyncStats(goldcraft::diagnostics::Stats* out) { *out = dispatcher.stats(); }
void AsyncStop() { dispatcher.stop(); }
}
