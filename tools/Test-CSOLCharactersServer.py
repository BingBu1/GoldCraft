"""Execute the unchanged character controller with controlled engine boundaries.

Uses the production catalog parser, wire protocol, access token and complete
characters.cpp body. It does not simulate actual MDL rendering or run a server.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
PRELUDE = r'''
#include "CHARACTER_CATALOG_HEADER"
#include "CHARACTER_PROTOCOL_HEADER"
#include "CHARACTER_SERVER_HEADER"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
constexpr int FL_KILLME=1,FL_FAKECLIENT=2,MSG_ONE=1,at_console=1,at_error=2;
constexpr int TERRORIST=1,CT=2,Menu_OFF=0,Menu_ChooseAppearance=1,Menu_ChooseTeam=2,PICKINGTEAM=1,JOINED=2;
constexpr int HUD_PRINTCENTER=1,VGUI_Menu_Team=1,MENU_KEY_1=1,MENU_KEY_2=2,MENU_KEY_5=16,MENU_KEY_0=512;
struct CBaseEntity;
struct entvars_t {int flags{},skin{};std::string model="models/player/urban/urban.mdl";};
struct edict_s {int serial=1,userid=10;CBaseEntity* object{};};
using edict_t=edict_s;
struct Globals {float time=10;int maxClients=32;} globals;
Globals* gpGlobals=&globals;
int rulesA{},rulesB{};int* g_pGameRules=&rulesA;
bool scenario=true,zombieMode=false,zombieHuman=false,extended=true,appearAllowed=true;
int nativeAppearances{},selectionNotifications{},unavailable{},checks{},loads{},frees{};
std::function<void()> appearanceCallback;
std::map<std::string,std::string> files;
std::array<CBasePlayer*,33> players{};
std::vector<std::string> args;
std::vector<std::vector<std::uint8_t>> packets;
struct CBaseEntity {virtual ~CBaseEntity()=default;};
struct CBasePlayer:CBaseEntity {
    entvars_t vars;entvars_t* pev=&vars;edict_t entity;std::string modelKey="urban";
    int slot=1,m_iTeam=CT,m_iMenu=Menu_ChooseAppearance,m_iJoiningState=PICKINGTEAM;
    bool live=false,dormant=false,m_bJustConnected=true,m_bTeamChanged=true,m_bHasC4=false;
    explicit CBasePlayer(int s=1):slot(s){entity.object=this;players[slot]=this;}
    ~CBasePlayer(){if(players[slot]==this)players[slot]=nullptr;}
    int entindex()const{return slot;}edict_t* edict(){return &entity;}
    bool IsDormant(){return dormant;}bool IsAlive(){return live;}
    void SetNewPlayerModel(const char* path){vars.model=path;}
    void SetPlayerModel(bool){modelKey=m_iTeam==TERRORIST?"terror":"urban";CSOL_ApplyCharacterModel(this);}
    static CBasePlayer* Instance(edict_t* e){return dynamic_cast<CBasePlayer*>(e->object);}
};
struct EHANDLE {
    CBasePlayer* player;int serial;
    explicit EHANDLE(CBasePlayer* p):player(p),serial(p->entity.serial){}
    edict_t* Get()const{return player->entity.serial==serial?&player->entity:nullptr;}
    template<class T>T* Get()const{return Get()?dynamic_cast<T*>(Get()->object):nullptr;}
};
bool CSOL_UsesScenarioRules(){return scenario;}
bool CSOL_ZombieMode(){return zombieMode;}
bool CSOL_ZombieHuman(CBasePlayer*){return zombieHuman;}
bool CSOL_CanUseExtendedWeapons(CBasePlayer*){return extended;}
int GETPLAYERUSERID(edict_t* p){return p->userid;}
CBasePlayer* UTIL_PlayerByIndex(int i){return players[i];}
int Q_strcmp(const char* a,const char* b){return std::strcmp(a,b);}
char* GET_INFO_BUFFER(edict_t* e){return reinterpret_cast<char*>(&CBasePlayer::Instance(e)->modelKey);}
const char* GET_KEY_VALUE(char* p,const char*){return reinterpret_cast<std::string*>(p)->c_str();}
void SET_CLIENT_KEY_VALUE(int,char* p,const char*,const char* v){*reinterpret_cast<std::string*>(p)=v;}
int CMD_ARGC_(){return int(args.size());}const char* CMD_ARGV_(int n){return args.at(n).c_str();}
void ClientPrint(entvars_t*,int,const char*){++unavailable;}
void ShowVGUIMenu(CBasePlayer*,int,int,const char*){}
bool CSOL_ShowScenarioTeamMenu(CBasePlayer*)=delete;
bool CSOL_ShowScenarioTeamMenu(CBasePlayer&){return scenario;}
void CSOL_ScenarioCharacterSelected(CBasePlayer&){++selectionNotifications;}
void HandleMenu_ChooseAppearance(CBasePlayer* p,int){
    ++nativeAppearances;
    if(appearanceCallback){auto action=std::move(appearanceCallback);action();return;}
    if(!appearAllowed)return;
    p->m_iMenu=Menu_OFF;p->m_iJoiningState=JOINED;p->m_bJustConnected=false;p->live=true;
}
unsigned char* LOAD_FILE_FOR_ME(const char* path,int* size){
    ++loads;auto f=files.find(path);if(f==files.end())return nullptr;
    *size=int(f->second.size());auto* bytes=new unsigned char[*size];std::memcpy(bytes,f->second.data(),*size);return bytes;
}
void FREE_FILE(void* p){++frees;delete[] static_cast<unsigned char*>(p);}
int REG_USER_MSG(const char*,int){return 1;}void ALERT(int,const char*,...){}
int PRECACHE_MODEL(char*){return 1;}int PRECACHE_SOUND(char*){return 1;}int PRECACHE_GENERIC(char*){return 1;}
void MESSAGE_BEGIN(int,int,const float*,entvars_t*){packets.emplace_back();}
void WRITE_BYTE(int b){packets.back().push_back(std::uint8_t(b));}void MESSAGE_END(){}
#define STRING(value) ((value).c_str())
#define Q_snprintf std::snprintf
'''
TESTS = r'''
void Check(bool condition,const char* why){++checks;if(!condition)throw std::runtime_error(why);}
void Command(CBasePlayer& p,const char* command,const char* argument=nullptr){
    args={command};if(argument)args.emplace_back(argument);gpGlobals->time+=.5f;
    Check(CSOL_CharacterCommand(p.edict(),command),"production command consumed");
}
bool ActiveWire(int id,int user){
    return std::any_of(packets.begin(),packets.end(),[&](const auto& bytes){csol::CharacterState s;return csol::DecodeCharacter(bytes,s)&&s.slot==1&&s.active&&s.id==id&&s.userid==user;});
}
void Resources(){
    files["models/csol/characters.json"]=R"({"version":1,"characters":[
      {"id":149,"team":2,"skin":0,"male":true,"allowPlayerStyle":false,"handStyle":0,"scenarioClass":49,"name":"CT","model":"fixture_ct","hand":"","portrait":"gfx/csol/p/fixture_ct.tga"},
      {"id":150,"team":1,"skin":2,"male":false,"allowPlayerStyle":false,"handStyle":1,"scenarioClass":50,"name":"T","model":"fixture_t","hand":"female","portrait":"gfx/csol/p/fixture_t.tga"},
      {"id":151,"team":1,"skin":0,"male":false,"allowPlayerStyle":false,"handStyle":0,"name":"Unmapped","model":"unmapped","hand":"","portrait":"gfx/csol/p/unmapped.tga"}]})";
    std::string manifest;
    for(const char* model:{"fixture_ct","fixture_t","unmapped"}){
        for(const auto& path:{std::string("models/player/")+model+"/"+model+".mdl",std::string("gfx/csol/p/")+model+".tga"}){files[path]="fixture";manifest+=path+"\n";}
    }
    files["shader/csol/characters.txt"]=manifest;CSOL_PrecacheCharacters();
    Check(loads==frees,"actual resource loader releases all buffers");
}
int main(){try{
    Resources();CBasePlayer p;int classId=-1;
    Check(!CSOL_CharacterScenarioClass(&p,classId),"unselected spawn cannot invent a class");
    Command(p,"csol_character_select","150");
    Check(nativeAppearances==1&&selectionNotifications==1&&p.m_iJoiningState==JOINED&&p.m_iMenu==Menu_OFF,"T appearance retains native appearance completion");
    Check(p.m_iTeam==CT&&!p.m_bJustConnected,"T character selection preserves cooperative CT ownership");
    Check(CSOL_CharacterScenarioClass(&p,classId)&&classId==50,"T character maps to its own scenario skill/upgrade class on CT");
    Check(CSOL_CharacterModel(&p)&&std::string(CSOL_CharacterModel(&p))=="fixture_t"&&p.vars.skin==2&&p.modelKey=="fixture_t","selected model and skin survive cross-team scenario gate");
    Check(p.vars.model=="models/player/fixture_t/fixture_t.mdl"&&ActiveWire(150,10),"production model path and hand-selection wire publish T identity");
    p.live=false;CSOL_UpdateCharacters(&p);classId=-1;
    Check(!CSOL_CharacterModel(&p)&&CSOL_CharacterScenarioClass(&p,classId)&&classId==50,"dead/pre-spawn class lookup retains T identity while visible model is inactive");
    p.live=true;p.vars.model="models/player/urban/urban.mdl";p.modelKey="urban";CSOL_ApplyCharacterModel(&p);CSOL_UpdateCharacters(&p);
    Check(p.modelKey=="fixture_t"&&p.vars.skin==2&&p.m_iTeam==CT,"respawn model application retains T selection without team transfer");
    p.vars.flags|=FL_FAKECLIENT;Check(CSOL_EnsureScenarioBotCharacter(&p)&&CSOL_CharacterScenarioClass(&p,classId)&&classId==50,"bot fallback preserves an existing opposite-catalog-team selection");p.vars.flags=0;
    Command(p,"csol_character_select","151");Check(unavailable==1&&CSOL_CharacterScenarioClass(&p,classId)&&classId==50,"unmapped class remains rejected without replacing old identity");
    Command(p,"csol_character_select","65535");Check(unavailable==2&&CSOL_CharacterModel(&p)&&p.modelKey=="fixture_t","unknown catalog ID cannot replace current model");
    auto access=std::make_shared<CSOLScenarioEntityAccess>();access->Revoke();classId=123;
    Check(!CSOL_CharacterScenarioClass(&p,classId,access)&&classId==123&&!CSOL_EnsureScenarioBotCharacter(&p,access),"revoked map access cannot resolve or create a class");
    scenario=false;CSOL_CharacterRoleChanged(&p);classId=-1;
    Check(!CSOL_CharacterModel(&p)&&!CSOL_CharacterScenarioClass(&p,classId)&&p.modelKey=="urban"&&p.vars.skin==0,"ordinary CT restores native model and disallows opposing T class");
    p.m_iTeam=TERRORIST;CSOL_UpdateCharacters(&p);
    Check(CSOL_CharacterModel(&p)&&CSOL_CharacterScenarioClass(&p,classId)&&classId==50,"ordinary matching T character remains supported");
    p.m_iTeam=3;Check(!CSOL_CharacterModel(&p)&&!CSOL_CharacterScenarioClass(&p,classId),"spectators cannot use character class");
    p.m_iTeam=CT;zombieMode=true;zombieHuman=true;CSOL_CharacterRoleChanged(&p);
    Check(CSOL_CharacterModel(&p)&&p.modelKey=="fixture_t","existing ZP human cross-team appearance remains supported");
    zombieHuman=false;CSOL_CharacterRoleChanged(&p);Check(!CSOL_CharacterModel(&p)&&p.modelKey=="urban","ZP zombie cannot retain human character model");
    zombieMode=false;scenario=true;p.live=false;Command(p,"csol_character_select","149");
    Check(unavailable==3&&CSOL_CharacterScenarioClass(&p,classId)&&classId==50,"dead non-menu command cannot replace class");
    p.live=true;++p.entity.userid;classId=123;
    Check(!CSOL_CharacterScenarioClass(&p,classId)&&classId==123,"new userid cannot inherit the previous slot class");
    CSOL_UpdateCharacters(&p);Check(!CSOL_CharacterModel(&p),"fresh slot generation has no selected model");
    p.vars.flags=FL_FAKECLIENT;Check(CSOL_EnsureScenarioBotCharacter(&p)&&CSOL_CharacterScenarioClass(&p,classId)&&classId==49,"unselected CT bot gets a compatible verified fallback");
    CSOL_CharacterDisconnected(&p);Check(!CSOL_CharacterScenarioClass(&p,classId),"disconnect retires character identity");
    p.vars.flags=0;p.m_iMenu=Menu_ChooseAppearance;p.m_iJoiningState=PICKINGTEAM;p.m_bJustConnected=true;p.m_bTeamChanged=true;
    Command(p,"csol_character_cancel");Check(p.m_iMenu==Menu_ChooseTeam&&!p.m_bTeamChanged&&p.m_iTeam==CT,"initial cancellation releases native team-change guard without team mutation");
    p.m_iMenu=Menu_ChooseAppearance;p.live=false;const auto oldPackets=packets.size();appearanceCallback=[&]{++p.entity.serial;};
    Command(p,"csol_character_select","150");Check(packets.size()==oldPackets&&nativeAppearances==2,"native appearance entity retirement prevents stale apply/broadcast");
    std::cout<<"character controller: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Output must stay in workspace build')
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'native/csol/server/characters.cpp'
    before = source.read_bytes()
    body = re.sub(r'^#include[^\n]*\n', '', before.decode('utf-8'), flags=re.M)
    prelude = PRELUDE
    for name, path in [('CHARACTER_CATALOG_HEADER', 'native/csol/character_catalog.hpp'),
                       ('CHARACTER_PROTOCOL_HEADER', 'native/csol/character_protocol.hpp'),
                       ('CHARACTER_SERVER_HEADER', 'native/csol/server/characters.h')]:
        prelude = prelude.replace(name, (ROOT / path).as_posix())
    cpp = output / 'characters-fixture.cpp'
    cpp.write_text(prelude + body + TESTS, encoding='utf-8')
    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT', '/utf-8', '/O2',
             '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise']
    commands = [[args.clang, *flags, '/c', str(cpp), '/Fo' + str(output / 'characters-fixture.obj')],
                [args.clang, *flags, '/I' + str(ROOT / 'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include'),
                 '/c', str(ROOT / 'native/csol/character_catalog.cpp'), '/Fo' + str(output / 'catalog.obj')],
                [args.clang, '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT',
                 str(output / 'characters-fixture.obj'), str(output / 'catalog.obj'),
                 '/Fe' + str(output / 'characters-fixture.exe'), '/link', '/OPT:REF', '/OPT:LLDLTO=3', '/threads:1']]
    for i, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True)
        (output / f'build-{i}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(f'Character fixture build {i} failed; see build log')
    result = subprocess.run([str(output / 'characters-fixture.exe')], capture_output=True, text=True)
    (output / 'results.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    print(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError('Character fixture checks failed')
    if source.read_bytes() != before:
        raise RuntimeError('Production source changed during test; rerun against final version')
    (output / 'evidence.json').write_text(json.dumps(dict(sourceSha256=hashlib.sha256(before).hexdigest(),
        sourceBodyUnchanged=True, sdkBoundariesStubbed=True, liveGameplay=False, commands=commands), indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
