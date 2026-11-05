// SPDX-License-Identifier: GPL-3.0-or-later
#include <utility>
using std::swap;
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <metahook.h>
#include <vgui_controls/Controls.h>
#ifndef OLD_CACHE
#include <VGUI/IResourceRefresh.h>
#endif
#include "stubs.hpp"
#define private public
#include "FontManager.h"
#ifndef OLD_CACHE
#include "FontTextureCache.h"
#endif
#include "Scheme2.h"
#undef private

extern IEngineSurface* staticSurface;
extern IEngineSurface_HL25* staticSurface_HL25;
extern IKeyValuesSystem* g_pKeyValuesSystem;
extern char m_szCurrentGameLanguage[128];

namespace fs = std::filesystem;
static unsigned checks{}, failures{};
static void Check(bool value, const char* name)
{ ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", name); } }

struct Keys final : IKeyValuesSystem
{
    std::vector<std::string> symbols;
    int allocations{};
    void RegisterSizeofKeyValues(int) override {}
    void* AllocKeyValuesMemory(int size) override { ++allocations; return std::calloc(1, size); }
    void FreeKeyValuesMemory(void* p) override { --allocations; std::free(p); }
    HKeySymbol GetSymbolForString(const char* name) override
    {
        for (std::size_t i=0;i<symbols.size();++i) if (!_stricmp(symbols[i].c_str(),name)) return static_cast<int>(i);
        symbols.emplace_back(name); return static_cast<int>(symbols.size()-1);
    }
    const char* GetStringForSymbol(HKeySymbol symbol) override { return symbols.at(symbol).c_str(); }
    void GetLocalizedFromANSI(const char*, wchar_t*, int) override {}
    void GetANSIFromLocalized(const wchar_t*, char*, int) override {}
    void AddKeyValuesToMemoryLeakList(void*, HKeySymbol) override {}
    void RemoveKeyValuesFromMemoryLeakList(void*) override {}
};

struct Files final : FileSystemStub
{
    fs::path directory;
    std::string font = "font-a.ttf";
    bool missing{};
    fs::path Resolve(const char* relative) const
    { return directory / (std::string(relative)=="fixture.ttf" ? font : relative); }
    const char* GetLocalPath(const char* relative,char* output,int maximum) override
    {
        if (missing) return nullptr;
        const auto path=Resolve(relative); if(!fs::is_regular_file(path)) return nullptr;
        auto text=path.string(); if(text.size()>=static_cast<std::size_t>(maximum))return nullptr;
        std::strcpy(output,text.c_str());return output;
    }
    FileHandle_t Open(const char* relative,const char* options,const char*) override
    { if(missing)return nullptr;return std::fopen(Resolve(relative).string().c_str(),options); }
    void Close(FileHandle_t file) override {std::fclose(static_cast<FILE*>(file));}
    unsigned Size(FileHandle_t file) override
    {auto* f=static_cast<FILE*>(file);auto p=std::ftell(f);std::fseek(f,0,SEEK_END);auto size=std::ftell(f);std::fseek(f,p,SEEK_SET);return size;}
    unsigned Size(const char* name) override {return fs::is_regular_file(Resolve(name))?static_cast<unsigned>(fs::file_size(Resolve(name))):0;}
    int Read(void* output,int size,FileHandle_t file) override {return static_cast<int>(std::fread(output,1,size,static_cast<FILE*>(file)));}
    bool IsOk(FileHandle_t file) override {return !std::ferror(static_cast<FILE*>(file));}
};

struct Surface final : vgui::SurfaceStub
{
    int flushes{}, proportionalWrites{};
    vgui::HFont CreateFont() override {return FontManager().CreateFont();}
    bool AddGlyphSetToFont(vgui::HFont font,const char* name,int tall,int weight,int blur,int scan,int flags) override
    {return FontManager().AddGlyphSetToFont(font,name,tall,weight,blur,scan,flags,0,0xffff);}
    bool AddCustomFontFile(const char* path) override
    {char local[MAX_PATH]{};return g_pFullFileSystem->GetLocalPath(path,local,sizeof(local))&&FontManager().AddCustomFontFile(local);}
    void DrawFlushText() override {++flushes;}
    void GetScreenSize(int& wide,int& tall) override {wide=1280;tall=720;}
    bool SupportsFeature(vgui::SurfaceFeature_e) override {return true;}
    void GetProportionalBase(int& wide,int& tall) override {wide=640;tall=480;}
    void GetHDProportionalBase(int& wide,int& tall) override {wide=1280;tall=720;}
    void SetProportionalBase(int,int) override {++proportionalWrites;}
    void SetHDProportionalBase(int,int) override {++proportionalWrites;}
};

struct EngineSurface final : EngineSurfaceStub
{
    int next{}, uploads{};
    std::map<int,std::vector<unsigned char>> textures;
    int createNewTextureID() override {return ++next;}
    void drawSetTextureRGBA(int id,const unsigned char* rgba,int wide,int tall,int,bool) override
    {textures[id].assign(rgba,rgba+wide*tall*4);}
    bool drawSetSubTextureRGBA(int id,int x,int y,const unsigned char* rgba,int wide,int tall) override
    {
        ++uploads;
        Check(x>=0 && y>=0 && x+wide<=256 && y+tall<=256,"texture write bounds");
        for(int row=0;row<tall;++row)std::memcpy(textures[id].data()+((y+row)*256+x)*4,rgba+row*wide*4,wide*4);
        return true;
    }
};

static void Scheme(const fs::path& directory,int tall,const char* extra="",const char* family="GoldCraft Refresh Fixture")
{
    std::ofstream file(directory/"fixture.res");
    file << "\"Scheme\" { \"CustomFontFiles\" { \"1\" \"fixture.ttf\" } \"Fonts\" { \"EngineFont\" { \"1\" { \"name\" \""
         << family << "\" \"tall\" \"" << tall << "\" \"weight\" \"400\" \"custom\" \"1\" \"yres\" \"1 6000\" } } } " << extra << " }";
}
static std::vector<unsigned char> Glyph(EngineSurface& surface,HFont font,wchar_t ch)
{
#ifdef OLD_CACHE
    auto* native=FontManager().GetFontForChar(font,ch);if(!native)return {};
    int a,b,c;native->GetCharABCWidths(ch,a,b,c);
    std::vector<unsigned char> result(b*native->GetHeight()*4);
    native->GetCharRGBA(ch,0,0,b,native->GetHeight(),result.data());return result;
#else
    int texture{};float* coordinates{};
    if(!g_FontTextureCache.GetTextureForChar(font,ch,&texture,&coordinates))return {};
    const int x=static_cast<int>(coordinates[0]*256+.5f),y=static_cast<int>(coordinates[1]*256+.5f);
    const int w=static_cast<int>(coordinates[2]*256+.5f)-x,h=static_cast<int>(coordinates[3]*256+.5f)-y;
    std::vector<unsigned char> result;
    for(int row=0;row<h;++row)result.insert(result.end(),surface.textures[texture].begin()+((y+row)*256+x)*4,surface.textures[texture].begin()+((y+row)*256+x+w)*4);
    return result;
#endif
}

int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    // Heap lifetime intentionally outlasts the plugin's static destructors.
    auto* keys=new Keys;auto* files=new Files;auto* surface=new Surface;auto* engine=new EngineSurface;
    files->directory=fs::absolute(argv[1]);g_pKeyValuesSystem=keys;g_pFullFileSystem=files;
    g_pVGuiSurface2=surface;staticSurface=engine;staticSurface_HL25=nullptr;std::strcpy(m_szCurrentGameLanguage,"english");
#ifdef OLD_CACHE
    Scheme(files->directory,22);
    auto schemeHandle=g_pVGuiSchemeManager2->LoadSchemeFromFile("fixture.res","Fixture");
    auto* scheme=g_pVGuiSchemeManager2->GetIScheme(schemeHandle);
    auto font=scheme->GetFont("EngineFont",false);const auto first=Glyph(*engine,font,L'M');
    Check(!first.empty(),"old source initial real font");
    files->font="font-b.ttf";Scheme(files->directory,30);
    g_pVGuiSchemeManager2->LoadSchemeFromFile("fixture.res","Fixture");
    Check(FontManager().GetFontTall(font)!=22,"old source must reread scheme recipe");
    Check(Glyph(*engine,font,L'M')!=first,"old source must replace cached native font");
    ::RemoveFontResourceA((files->directory/"font-a.ttf").string().c_str());
    std::printf("old-source checks=%u failures=%u\n",checks,failures);return failures?1:0;
#else
    auto* refresh=static_cast<vgui::IResourceRefresh*>(CreateInterface(VGUI_RESOURCE_REFRESH_INTERFACE_VERSION,nullptr));
    Check(refresh!=nullptr,"versioned factory interface");if(!refresh)return 3;
    using Result=vgui::ResourceRefreshResult;
    vgui::ResourceRefreshRequest empty{sizeof(empty),nullptr,0,nullptr,0,nullptr};
    unsigned long long token{},nested{};
    Check(refresh->BeginUpdate(empty,token)==Result::Success && token,"empty initial transaction");
    Check(refresh->BeginUpdate(empty,nested)==Result::Busy && !nested,"busy preserves active transaction");
    Check(refresh->CommitUpdate(token)==Result::Success,"empty commit");
    Check(refresh->AbortUpdate(token)==Result::StaleToken,"completed token is stale");
    auto& manager=FontManager();
    const auto ordinary=manager.CreateFont();
    Check(manager.AddGlyphSetToFont(ordinary,"Arial",22,400,0,0,vgui::FONTFLAG_CUSTOM,0,0xffff),"ordinary font");
    auto ordinaryGlyph=Glyph(*engine,ordinary,L'M');auto* ordinaryNative=manager.GetFontForChar(ordinary,L'M');
    Scheme(files->directory,22);
    auto schemeHandle=g_pVGuiSchemeManager2->LoadSchemeFromFile("fixture.res","Fixture");
    auto* scheme=static_cast<vgui::CScheme*>(g_pVGuiSchemeManager2->GetIScheme(schemeHandle));
    Check(schemeHandle!=0,"scheme initial load");
    const auto font=scheme->GetFont("EngineFont",false);
    const auto shared=manager.CreateFont();
    Check(manager.AddGlyphSetToFont(shared,"GoldCraft Refresh Fixture",22,400,0,0,vgui::FONTFLAG_CUSTOM,0,0xffff),"shared family owner");
    const auto before=Glyph(*engine,font,L'M');Check(!before.empty(),"initial GDI glyph");
    const auto sharedBefore=Glyph(*engine,shared,L'M');
    const auto initialTall=manager.GetFontTall(font);
    const auto oldDC=manager.GetFontForChar(font,L'M')->m_hDC;
    const auto oldFont=manager.GetFontForChar(font,L'M')->m_hFont;
    const auto handles=manager.m_FontAmalgams.Count();const auto aliasCount=scheme->m_FontAliases.Count();
    const char* families[]={"GoldCraft Refresh Fixture"};const char* fontFiles[]={"fixture.ttf"};
    vgui::ResourceRefreshRequest request{sizeof(request),families,1,fontFiles,1,"fixture.res"};
    Check(refresh->BeginUpdate(request,token)==Result::Success,"begin targeted update");
    Check(manager.GetFontForChar(font,L'M')==nullptr && manager.GetFontForChar(shared,L'M')==nullptr,"shared family suspended");
    Check(!GetObjectType(oldDC) && !GetObjectType(oldFont),"old GDI selected objects actually deleted");
    Check(!manager.AddGlyphSetToFont(shared,"Arial",22,400,0,0,vgui::FONTFLAG_CUSTOM,0,0xffff),"pending handle cannot be repopulated");
    Check(manager.AddCustomFontFile((files->directory/"font-memory-a.ttf").string().c_str()),"unrelated file registration permitted while pending");
    Check(manager.GetFontForChar(ordinary,L'M')==ordinaryNative && Glyph(*engine,ordinary,L'M')==ordinaryGlyph,"unrelated family and cache survive begin");
    Result wrong{};std::thread worker([&]{wrong=refresh->CommitUpdate(token);});worker.join();Check(wrong==Result::WrongThread,"wrong-thread commit rejected");
    files->font="font-b.ttf";Scheme(files->directory,30);
    Check(refresh->CommitUpdate(token)==Result::Success,"replace font bytes and scheme");
    Check(scheme->GetFont("EngineFont",false)==font && scheme->m_FontAliases.Count()==aliasCount,"stable scheme font aliases");
    std::printf("font height old=%d new=%d recipe=%d bytes old=%zu new=%zu changed=%d\n",initialTall,manager.GetFontTall(font),manager.m_FontSpecs[font].tall,before.size(),Glyph(*engine,font,L'M').size(),before!=Glyph(*engine,font,L'M'));
    Check(manager.m_FontSpecs[font].tall==30 && manager.GetFontTall(font)!=initialTall && before!=Glyph(*engine,font,L'M'),"new scheme height and real glyph bytes");
    Check(manager.m_FontSpecs[shared].tall==22,"shared owner recipe preserved");
    Check(Glyph(*engine,shared,L'M')!=sharedBefore,"same recipe loads new font body");
    Check(manager.m_FontAmalgams.Count()==handles,"no new handles");
    Check(surface->proportionalWrites==2,"refresh does not rewrite global proportional base");
    const int kvCount=keys->allocations;
    const auto nativeCount=manager.m_Win32Fonts.Count();
    // Warm both page sizes before measuring repeated reuse and GDI ownership.
    Glyph(*engine,shared,L'M');const int pages=g_FontTextureCache.m_PageList.Count();
    const auto gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int iteration=0;iteration<40;++iteration)
    {
        Check(refresh->BeginUpdate(request,token)==Result::Success,"repeat begin");
        Check(refresh->CommitUpdate(token)==Result::Success,"repeat commit");
        Check(!Glyph(*engine,font,L'M').empty()&&!Glyph(*engine,shared,L'M').empty(),"repeat glyph draw");
        Check(Glyph(*engine,ordinary,L'M')==ordinaryGlyph,"shared page ordinary bytes stable");
    }
    Check(manager.m_FontAmalgams.Count()==handles && manager.m_Win32Fonts.Count()==nativeCount,"repeat handle/native ownership bounded");
    Check(g_FontTextureCache.m_PageList.Count()==pages,"repeat glyph pages bounded");
    Check(keys->allocations==kvCount,"old scheme KeyValues freed");
    Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=gdiBefore,"GDI object count bounded");
    Check(refresh->BeginUpdate(request,token)==Result::Success,"begin failure transaction");
    files->missing=true;Check(refresh->CommitUpdate(token)==Result::FileUnavailable,"missing scheme fails");
    Check(manager.GetFontForChar(font,L'M')==nullptr,"failure remains suspended");
    files->missing=false;Scheme(files->directory,30);
    Check(refresh->AbortUpdate(token)==Result::Success && manager.m_FontSpecs[font].tall==30,"old VFS abort restores recipe");
    Check(refresh->BeginUpdate(request,token)==Result::Success,"begin unsupported borders");
    Scheme(files->directory,30,"\"Borders\" { \"NewBorder\" { \"1\" \"2\" } }");
    Check(refresh->CommitUpdate(token)==Result::UnsupportedScheme,"borders rejected without invalidating panels");
    Scheme(files->directory,30);Check(refresh->AbortUpdate(token)==Result::Success,"unsupported scheme rollback");
    Check(refresh->BeginUpdate(request,token)==Result::Success,"begin invalid font file");
    std::ofstream(files->directory/"broken.ttf") << "not a font";files->font="broken.ttf";
    Check(refresh->CommitUpdate(token)==Result::FileUnavailable,"invalid GDI font file rejects commit");
    Check(manager.GetFontForChar(font,L'M')==nullptr,"invalid font file stays suspended");
    files->font="font-b.ttf";Check(refresh->AbortUpdate(token)==Result::Success,"invalid font file rollback");
    auto badRequest=request;badRequest.size=0;
    Check(refresh->BeginUpdate(badRequest,nested)==Result::InvalidRequest && !nested,"invalid ABI size rejected before mutation");
    badRequest=request;badRequest.schemeFile="../outside.res";
    Check(refresh->BeginUpdate(badRequest,nested)==Result::InvalidRequest && !nested,"relative path traversal rejected");
    Check(refresh->BeginUpdate(request,token)==Result::Success,"begin staged invalid family");
    vgui::ResourceFontDefinition absent{sizeof(absent),"GC This Family Is Missing",28,400,0,0,vgui::FONTFLAG_CUSTOM,0,0xffff};
    Check(refresh->StageFont(token,shared,absent)==Result::Success,"stage absent font");
    Check(refresh->CommitUpdate(token)==Result::FontUnavailable,"missing custom family rejects commit");
    Check(refresh->AbortUpdate(token)==Result::Success && manager.m_FontSpecs[shared].name=="GoldCraft Refresh Fixture","abort restores old staged recipe");
    Check(refresh->BeginUpdate(request,token)==Result::Success,"begin staged owner recipe");
    vgui::ResourceFontDefinition definition{sizeof(definition),"Arial",28,700,0,0,vgui::FONTFLAG_CUSTOM,0,0xffff};
    Check(refresh->StageFont(token,ordinary,definition)==Result::InvalidRequest,"cannot change unrelated handle");
    Check(refresh->StageFont(token,font,definition)==Result::InvalidRequest,"scheme aliases stay owned by scheme");
    Check(refresh->StageFont(token,shared,definition)==Result::Success,"stage menu owner font");
    Check(refresh->CommitUpdate(token)==Result::Success && manager.m_FontSpecs[shared].tall==28,"staged recipe committed in place");
    Check(manager.m_FontAmalgams.Count()==handles,"staged recipe keeps handle");
    // Include the staged family in the next boundary, exactly as the owner does.
    const char* removalFamilies[]={"GoldCraft Refresh Fixture","Arial"};
    request.fontFamilies=removalFamilies;request.fontFamilyCount=2;
    Check(refresh->BeginUpdate(request,token)==Result::Success,"begin overlay removal");
    files->missing=true;
    Check(refresh->CommitRemoval(token)==Result::Success,"base removal permits absent CSOL resources");
    Check(manager.GetFontForChar(font,L'M')==nullptr,"removed scheme alias dormant");
    Check(Glyph(*engine,ordinary,L'M')==ordinaryGlyph,"ordinary font restored from base");
    Check(g_pVGuiSchemeManager2->LoadSchemeFromFile("fixture.res","Fixture")==0,"retired scheme cannot return stale KV");
    Check(refresh->BeginUpdate(request,token)==Result::Success,"begin from dormant base");
    Check(refresh->AbortUpdate(token)==Result::Success && !manager.GetFontForChar(font,L'M'),"cancel reentry keeps dormant alias");
    Check(refresh->BeginUpdate(request,token)==Result::Success,"begin reentry");
    files->missing=false;files->font="font-a.ttf";Scheme(files->directory,22);
    Check(refresh->CommitUpdate(token)==Result::Success,"new CSOL overlay revives cached handle");
    Check(scheme->GetFont("EngineFont",false)==font && manager.m_FontSpecs[font].tall==22,"reentry stable handles and fresh recipe");
    Check(!Glyph(*engine,font,L'M').empty(),"reentry renders real glyph");
    Check(manager.m_FontAmalgams.Count()==handles,"reentry does not leak handles");
    const auto externalFile=(files->directory/"font-a.ttf").string();
    Check(::AddFontResourceA(externalFile.c_str())>0,"external font owner registers same file");
    manager.RemoveCustomFontFiles();
    Check(manager.m_FontFiles.empty(),"ordinary shutdown releases owned registration counts");
    Check(::RemoveFontResourceA(externalFile.c_str())!=0,"shutdown preserves external owner's registration");
    const auto memoryBytes=[](const fs::path& path)
    {std::ifstream in(path,std::ios::binary);return std::vector<char>(std::istreambuf_iterator<char>(in),{});};
    auto bytesA=memoryBytes(files->directory/"font-memory-a.ttf"),bytesB=memoryBytes(files->directory/"font-memory-b.ttf");
    DWORD installed{};auto memory=AddFontMemResourceEx(bytesA.data(),static_cast<DWORD>(bytesA.size()),nullptr,&installed);
    Check(memory && installed,"owner private memory font registered");
    auto memoryFont=manager.CreateFont();
    manager.AddGlyphSetToFont(memoryFont,"GoldCraft Memory Fixture",22,400,0,0,vgui::FONTFLAG_CUSTOM,0,0xffff);
    const auto memoryOld=Glyph(*engine,memoryFont,L'M');
    Check(!memoryOld.empty(),"private nonenumerable font usable by actual VGUI manager");
    const char* memoryFamilies[]={"GoldCraft Memory Fixture"};
    vgui::ResourceRefreshRequest memoryRequest{sizeof(memoryRequest),memoryFamilies,1,nullptr,0,nullptr};
    Check(refresh->BeginUpdate(memoryRequest,token)==Result::Success,"private memory begin");
    Check(RemoveFontMemResourceEx(memory)!=0,"caller removes old private font after Begin");
    memory=AddFontMemResourceEx(bytesB.data(),static_cast<DWORD>(bytesB.size()),nullptr,&installed);
    Check(memory && installed,"caller installs replacement private font before Commit");
    Check(refresh->CommitUpdate(token)==Result::Success,"private memory commit");
    Check(!Glyph(*engine,memoryFont,L'M').empty() && Glyph(*engine,memoryFont,L'M')!=memoryOld,"private same-family bytes update through GDI");
    Check(refresh->BeginUpdate(memoryRequest,token)==Result::Success,"private memory removal begin");
    Check(RemoveFontMemResourceEx(memory)!=0,"caller removes private font for base");
    Check(refresh->CommitRemoval(token)==Result::Success && !manager.GetFontForChar(memoryFont,L'M'),"private memory absent in base leaves dormant handle");
    std::printf("checks=%u failures=%u GDI=%lu pages=%d HFont=%d\n",checks,failures,GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),g_FontTextureCache.m_PageList.Count(),manager.m_FontAmalgams.Count());
    return failures?1:0;
#endif
}
