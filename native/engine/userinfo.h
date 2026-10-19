#pragma once
#include <cstddef>

void GC_UserInfoInit();
void GC_UserInfoBegin(char* legacy);
void GC_UserInfoForget(char* legacy);
void GC_UserInfoReset();
const char* GC_UserInfoRead(const char* legacy);
bool GC_UserInfoExtended(const char* legacy);
bool GC_UserInfoDefer(client_t* source, sizebuf_t* message, std::size_t bytes);
void GC_UserInfoFlush();

// Only registered client buffers are redirected. Localinfo, serverinfo,
// hook-owned temporary buffers and the engine ABI retain their original size.
struct GC_UserInfoMutation {
    char* legacy;
    char* data;
    explicit GC_UserInfoMutation(char* input);
    ~GC_UserInfoMutation();
    bool extended() const { return data != legacy; }
};
