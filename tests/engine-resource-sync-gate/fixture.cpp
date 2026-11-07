// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "precache.h"
#include "resource_sync_hash.hpp"
#include "goldcraft/resource_stamp.hpp"
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <thread>
#include <vector>

using goldcraft::filesystem::ResourceStamp;
using goldcraft::filesystem::ResourceStampAPI;
using goldcraft::resource_sync::Digest;
using Bytes=std::vector<unsigned char>;
static unsigned checks{},opens{},closes{},reads{},queries{};
static const auto main_thread=std::this_thread::get_id();
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": " #x <<'\n';std::abort();}}while(0)
static void main_only(){CHECK(std::this_thread::get_id()==main_thread);}
GoldCraftPrecache gc_precache;
struct File {Bytes bytes; ResourceStamp stamp; bool broken{};};
struct Handle {std::shared_ptr<File> file;};
static std::map<std::string,std::shared_ptr<File>> files;
static std::size_t cursor{};
static std::function<void()> after_read;
static bool metadata_enabled=true;
IFileSystem* g_pFileSystem=reinterpret_cast<IFileSystem*>(0x1234);
static int stamp(const std::shared_ptr<File>& file,ResourceStamp* out,unsigned size){
    main_only();if(!metadata_enabled||!file||size!=sizeof(*out))return 0;
    *out=file->stamp;out->length=file->bytes.size();return 1;
}
static int query(const void* owner,const char* name,ResourceStamp* out,unsigned size){
    ++queries;CHECK(owner==g_pFileSystem);const auto it=files.find(name);
    return it==files.end()?0:stamp(it->second,out,size);
}
static int opened(const void* owner,void* handle,ResourceStamp* out,unsigned size){
    CHECK(owner==g_pFileSystem);return stamp(reinterpret_cast<Handle*>(handle)->file,out,size);
}
static ResourceStampAPI provider{sizeof(ResourceStampAPI),query,opened};
static IBaseInterface* factory(const char* name,int*){
    CHECK(std::strcmp(name,goldcraft::filesystem::ResourceStampVersion)==0);return reinterpret_cast<IBaseInterface*>(&provider);
}
CreateInterfaceFn g_FileSystemFactory=factory;
FileHandle_t FS_Open(const char* name,const char*){
    main_only();auto it=files.find(name);if(it==files.end())return nullptr;++opens;return reinterpret_cast<FileHandle_t>(new Handle{it->second});
}
void FS_Close(FileHandle_t handle){main_only();++closes;delete reinterpret_cast<Handle*>(handle);}
unsigned FS_Size(FileHandle_t handle){main_only();return unsigned(reinterpret_cast<Handle*>(handle)->file->bytes.size());}
void FS_Seek(FileHandle_t,int offset,FileSystemSeek_t){main_only();cursor=offset;}
int FS_IsOk(FileHandle_t handle){main_only();return !reinterpret_cast<Handle*>(handle)->file->broken;}
int FS_Read(void* output,int size,int count,FileHandle_t handle){
    main_only();++reads;const auto& body=reinterpret_cast<Handle*>(handle)->file->bytes;
    const auto n=(std::min)(std::size_t(size*count),body.size()-(std::min)(cursor,body.size()));
    std::memcpy(output,body.data()+cursor,n);cursor+=n;
    if(after_read){auto callback=std::move(after_read);after_read={};callback();}return int(n);
}
void Con_Printf(const char* fmt,...){main_only();}
const char* Info_ValueForKey(const char* text,const char* key){
    static std::string result;result.clear();std::string source(text),needle=std::string("\\")+key+"\\";
    auto at=source.find(needle);if(at!=source.npos){at+=needle.size();result=source.substr(at,source.find('\\',at)-at);}return result.data();
}
bool GC_PrecacheClient(const client_t* client){return client&&std::strcmp(Info_ValueForKey(client->userinfo,"gc_precache"),"3")==0;}
static void add(const char* name,resourcetype_t type,int size,int flags=0){
    resource_t item{};std::strcpy(item.szFileName,name);item.type=type;item.nDownloadSize=size;item.ucFlags=flags;
    gc_precache.resources.push_back(item);
}
static std::shared_ptr<File> install(const char* name,Bytes bytes,unsigned identity){
    auto file=std::make_shared<File>();file->bytes=std::move(bytes);file->stamp={1,identity,0,file->bytes.size(),1,2,3,0,file->bytes.size(),0};files[name]=file;return file;
}
static unsigned http_publications{};
void GC_HttpResources(){++http_publications;}
void NORETURN Host_Error(const char* format,...){throw std::runtime_error(format);}
extern "C" char* A_strncpy(char* out,const char* in,size_t size){return std::strncpy(out,in,size);}
#include "map-list-tail.inc"
#include "resource-sync-under-test.inc"
static Digest digest(const Bytes& bytes){goldcraft::resource_sync::Hasher h;Digest value{};CHECK(h.Update(bytes.data(),bytes.size()));CHECK(h.Finish(value));return value;}
static std::string hex(const Digest& value){std::string out;for(auto c:value){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out;}
static void reset(){GC_ResourceSyncReset();gc_precache.resources.clear();after_read={};CHECK(opens==closes);}

int main(){
    const auto tail_ok=[] {CompleteResourceList();CHECK(http_publications>0);};
    if(!GC_ResourceSyncEnabled()){
        client_t advertised{};std::strcpy(advertised.userinfo,"\\gc_precache\\3\\gc_resource_sync\\1");
        for(unsigned map=0;map<3;++map){
            reset();add("models/missing.mdl",t_model,3);
            const auto previous=http_publications;tail_ok();CHECK(http_publications==previous+1);
            CHECK(!GC_ResourceSyncClient(&advertised));CHECK(!GC_ResourceSyncReady());CHECK(!GC_ResourceSyncGeneration());CHECK(!GC_ResourceSyncEntry(0));
            std::string error;CHECK(!GC_ResourceSyncBuild(error));CHECK(error.find("disabled")!=error.npos);
            CHECK(opens==0 && closes==0 && reads==0 && queries==0);
        }
        std::cout<<"resource sync gate OFF "<<checks<<" checks passed; no VFS IO across three map resets\n";return 0;
    }

    CHECK(hex(digest({}))=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hex(digest({'a','b','c'}))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    {Bytes million(1000000,'a');goldcraft::resource_sync::Hasher h;Digest value{};for(std::size_t at=0;at<million.size();at+=137)CHECK(h.Update(million.data()+at,(std::min)(std::size_t(137),million.size()-at)));CHECK(h.Finish(value));CHECK(hex(value)=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");CHECK(!h.Finish(value));}
    client_t client{};CHECK(!GC_ResourceSyncClient(nullptr));
    for(const char* info:{"","\\gc_precache\\3","\\gc_resource_sync\\1","\\gc_precache\\4\\gc_resource_sync\\1","\\gc_precache\\3\\gc_resource_sync\\2"}){std::strcpy(client.userinfo,info);CHECK(!GC_ResourceSyncClient(&client));}
    std::strcpy(client.userinfo,"\\gc_precache\\3\\gc_resource_sync\\1");CHECK(GC_ResourceSyncClient(&client));
    auto a=install("models/a.mdl",{'a','b','c'},1);
    auto sound=install("sound/a.wav",Bytes(700001,0x62),2);sound->stamp.packed=1;sound->stamp.offset=4096;sound->stamp.size=900000;
    add("models/a.mdl",t_model,3);add("a.wav",t_sound,700001);add("models/a.mdl",t_generic,3);
    add("*1",t_model,20);add("!sentence",t_sound,20);add("decal",t_decal,20);add("spray.wad",t_generic,20,RES_CUSTOM);add("server.cfg",t_generic,20);
    tail_ok();CHECK(GC_ResourceSyncReady());
    std::string error;CHECK(GC_ResourceSyncBuild(error));CHECK(error.empty());CHECK(GC_ResourceSyncReady());
    const auto first_generation=GC_ResourceSyncGeneration();CHECK(first_generation!=0);
    CHECK(GC_ResourceSyncEntry(0)->sha256==digest(a->bytes));CHECK(*GC_ResourceSyncEntry(0)==*GC_ResourceSyncEntry(2));
    CHECK(GC_ResourceSyncEntry(1)->sha256==digest(sound->bytes));CHECK(opens==2&&closes==2&&reads==4);
    for(unsigned i=3;i<8;++i)CHECK(!GC_ResourceSyncEntry(i)->required);CHECK(GC_ResourceSyncEntry(8)==nullptr);
    const auto cold_reads=reads,cold_opens=opens;GC_ResourceSyncReset();CHECK(!GC_ResourceSyncReady());CHECK(GC_ResourceSyncEntry(0)==nullptr);
    CHECK(GC_ResourceSyncBuild(error));CHECK(GC_ResourceSyncGeneration()!=first_generation);CHECK(reads==cold_reads&&opens==cold_opens);
    a->bytes[0]='d';++a->stamp.changed;CHECK(GC_ResourceSyncBuild(error));CHECK(reads==cold_reads+1);CHECK(GC_ResourceSyncEntry(0)->sha256==digest(a->bytes));
    ++sound->stamp.offset;CHECK(GC_ResourceSyncBuild(error));CHECK(reads==cold_reads+4);
    const auto before_swap=reads;a=install("models/a.mdl",{'n','e','w'},3);CHECK(GC_ResourceSyncBuild(error));CHECK(reads==before_swap+1&&GC_ResourceSyncEntry(0)->sha256==digest(a->bytes));
    metadata_enabled=false;const auto before_fallback=reads;CHECK(GC_ResourceSyncBuild(error));CHECK(GC_ResourceSyncBuild(error));CHECK(reads==before_fallback+8);metadata_enabled=true;
    ++a->stamp.changed;after_read=[&]{++a->stamp.changed;};CHECK(!GC_ResourceSyncBuild(error));CHECK(error.find("changed during")!=error.npos);CHECK(!GC_ResourceSyncReady());CHECK(opens==closes);
    after_read=[&]{a=install("models/a.mdl",{'n','e','w'},4);};CHECK(!GC_ResourceSyncBuild(error));CHECK(error.find("changed during")!=error.npos);CHECK(opens==closes);
    CHECK(GC_ResourceSyncBuild(error));a->broken=true;++a->stamp.changed;CHECK(!GC_ResourceSyncBuild(error));CHECK(!GC_ResourceSyncReady());CHECK(opens==closes);a->broken=false;
    reset();add("models/missing.mdl",t_model,3);
    const auto before_failure=http_publications;bool failed=false;
    try{CompleteResourceList();}catch(const std::runtime_error&){failed=true;}
    CHECK(failed && http_publications==before_failure);
    CHECK(!GC_ResourceSyncBuild(error));CHECK(error.find("before sync")!=error.npos);
    reset();add("models/a.mdl",t_model,4);CHECK(!GC_ResourceSyncBuild(error));
    reset();add("models/a.mdl",t_model,3);add("models/a.mdl",t_generic,4);CHECK(!GC_ResourceSyncBuild(error));CHECK(error.find("conflicting")!=error.npos);
    reset();add("models/huge.mdl",t_model,int(goldcraft::resource_sync::max_file_size)+1);CHECK(!GC_ResourceSyncBuild(error));CHECK(error.find("exceeds")!=error.npos);
    reset();add("models/../secret.mdl",t_model,3);CHECK(GC_ResourceSyncBuild(error));CHECK(!GC_ResourceSyncEntry(0)->required);
    reset();CHECK(GC_ResourceSyncBuild(error));CHECK(GC_ResourceSyncReady());CHECK(GC_ResourceSyncEntry(0)==nullptr);
    reset();tail_ok();CHECK(GC_ResourceSyncReady());
    std::cout<<"resource sync gate ON checks="<<checks<<" passed; opens="<<opens<<" closes="<<closes<<" reads="<<reads<<" metadata="<<queries<<'\n';
}
