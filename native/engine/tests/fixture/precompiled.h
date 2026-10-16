#pragma once
// Narrow engine API fixture. Production adapter code is compiled unchanged;
// these implementations assert that every filesystem call stays on main.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
struct cvar_t { const char* name; const char* string; int flags; float value; cvar_t* next; };
struct model_t {}; struct mod_known_info_t {}; struct client_t {}; struct delta_t {}; struct sizebuf_t {};
using qboolean = int;
enum resourcetype_t { t_sound, t_skin, t_model, t_decal, t_generic, t_eventscript, t_world };
constexpr int RES_CUSTOM = 4;
struct resource_t { char szFileName[64]{}; resourcetype_t type{}; int nDownloadSize{}; unsigned char ucFlags{}; };
using FileHandle_t = void*;
enum FileSystemSeek_t { FILESYSTEM_SEEK_HEAD };
extern cvar_t sv_downloadurl, sv_allow_download, sv_send_resources, sv_allow_dlfile;
extern double realtime;
void Cvar_RegisterVariable(cvar_t*);
void Cvar_Set(const char*, const char*);
void Cmd_AddCommand(const char*, void (*)());
void Con_Printf(const char*, ...);
FileHandle_t FS_Open(const char*, const char*);
void FS_Close(FileHandle_t);
unsigned FS_Size(FileHandle_t);
unsigned FS_FileSize(const char*);
int FS_GetFileTime(const char*);
int FS_IsOk(FileHandle_t);
void FS_Seek(FileHandle_t, int, FileSystemSeek_t);
int FS_Read(void*, int, int, FileHandle_t);
