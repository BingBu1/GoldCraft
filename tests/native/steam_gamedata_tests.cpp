// Exercise the matched production loader against generated catalogs, without
// loading/executing the engine, connecting Steam, or starting the game.
#include "../../external/MetaHookSv-20261007/MetaHook/src/GameData.cpp"
#include <cassert>
#include <filesystem>
#include <iostream>

bool MH_IsInLdrCriticalRegion(){return false;}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    std::vector<std::string> paths{argv[1]};
    for(const auto& entry:std::filesystem::recursive_directory_iterator(argv[1]))
        if(entry.is_regular_file()&&entry.path().filename()=="index.json"
           &&entry.path().parent_path()!=std::filesystem::path(argv[1]))
            paths.push_back(entry.path().parent_path().string());
    paths.push_back(argv[2]);std::vector<const char*> roots;
    for(const auto& path:paths)roots.push_back(path.c_str());
    assert(GameData::Initialize(roots.data(),roots.size()));
    struct Expected {std::uint64_t crc;const char* name;DWORD rva;};
    for(const auto& item:{Expected{0x6ef7192cd8254e2d,"GoldCraftSteam_UserContext",0x31d250},
                         Expected{0x124d6034a6b28b40,"GoldCraftSteam_FriendsContext",0xee0b4},
                         Expected{0x124d6034a6b28b40,"GoldCraftSteam_UtilsContext",0xee0c0}}){
        mh_gamesymbol_t symbol{};symbol.cbSize=sizeof(symbol);
        assert(MH_GAMESYMBOL_OK==GameData::QueryByCRC64(item.crc,item.name,&symbol));
        assert(symbol.kind==MH_GAMESYMBOL_KIND_GLOBAL&&symbol.rva==item.rva);
        assert(MH_GAMESYMBOL_OK!=GameData::QueryByCRC64(item.crc^1,item.name,&symbol));
    }
    // Existing symbols must remain available when the extra catalog is merged.
    mh_gamesymbol_t existing{};existing.cbSize=sizeof(existing);
    assert(MH_GAMESYMBOL_OK==GameData::QueryByCRC64(0x124d6034a6b28b40,"GameStudioRenderer_StudioDrawModel",&existing));
    std::cout<<"Production GameData loader: merged three exact Steam globals, rejected foreign modules and retained client symbols\n";
}
