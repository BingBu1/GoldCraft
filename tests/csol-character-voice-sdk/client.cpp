// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the production event consumer with actual MetaHook SDK types.
#include "../../native/csol/client/emotion_voice.cpp"
#include <iostream>
#include <fstream>
#include <set>
#include <iterator>

namespace fixture {
unsigned checks{}, failures{}, reads{}, probes{}, plays{}, warnings{};
std::string json, level="maps/zs_deadend.bsp", lastSound;
std::set<std::string> existing;
float lastVolume{};
bool characterPresent=true, throwRead{};
csol::Character character{};
void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<message<<'\n';}}
void __cdecl Log(const char*,...){++warnings;}
void Setup(std::string table){
 json=std::move(table); reads=probes=plays=warnings=0;lastSound.clear();characterPresent=true;throwRead=false;
 character={};character.id=0;character.male=true;existing.clear();
 csol::client::ResetEmotionVoices();
 csol::client::engine.pfnGetLevelName=[](){return level.c_str();};
 csol::client::engine.pfnPlaySoundByName=[](const char* path,float volume){++plays;lastSound=path;lastVolume=volume;};
 csol::client::engine.Con_Printf=&Log;
}
bool Event(const char* option,int code=5050){mstudioevent_t event{};event.event=code;std::strncpy(event.options,option,sizeof event.options-1);cl_entity_t entity{};return csol::client::EmotionVoiceStudioEvent(&event,&entity);}
}
namespace csol::client {
cl_enginefunc_t engine{};
const Character* ViewCharacter(){return fixture::characterPresent?&fixture::character:nullptr;}
std::vector<std::byte> ReadResource(const char* path){++fixture::reads;fixture::Check(std::string_view(path)=="sound/sentences.json","reads original table path");if(fixture::throwRead)throw std::runtime_error("fixture unavailable");const auto* start=reinterpret_cast<const std::byte*>(fixture::json.data());return {start,start+fixture::json.size()};}
bool Exists(const char* path){++fixture::probes;return fixture::existing.contains(path);}
}
int main(int argc,char** argv){
 using namespace fixture;using namespace csol::client;
 Setup(R"({"MRAD_EMOTION_HI":{"man":"weapons/man_hi.wav","woman":"weapons/woman_hi.wav","jin":"weapons/jin_hi.wav"},"MRAD_EMOTION_JOY":{"man":["weapons/joy1.wav","weapons/joy2.wav","weapons/joy3.wav"]}})");
 mstudioevent_t empty{};cl_entity_t entity{};
 Check(!EmotionVoiceStudioEvent(nullptr,&entity)&&!EmotionVoiceStudioEvent(&empty,nullptr),"null event/entity remains unhandled");
 Check(!Event("%!mrad_emotion_hi",5004)&&reads==0&&plays==0,"non5050 remains downstream without loading");
 characterPresent=false;Check(Event("%!mrad_emotion_hi")&&reads==0&&plays==0,"missing view character consumes without sound");characterPresent=true;
 std::srand(731);const int discarded=std::rand(),next=std::rand();(void)discarded;std::srand(731);
 Check(Event("%!mrad_emotion_hi")&&lastSound=="weapons/man_hi.wav"&&lastVolume==1&&plays==1,"5050 exact sentence and volume");
 Check(std::rand()==next,"single candidate still consumes original CRT rand call");
 character.male=false;Check(Event("%!mrad_emotion_hi")&&lastSound=="weapons/woman_hi.wav"&&reads==1,"character changes select a new voice without stale cache");
 character.id=76;Check(Event("%!mrad_emotion_hi")&&lastSound=="weapons/jin_hi.wav","model76 uses jin independent of male flag");
 character.id=100;Check(Event("%!mrad_emotion_hi")&&lastSound=="weapons/man_hi.wav","missing19s1man falls back to man");
 character.id=101;Check(Event("%!mrad_emotion_hi")&&lastSound=="weapons/woman_hi.wav","missing19s1woman falls back to woman");
 const auto before=plays;Check(Event("%!mrad_emotion_e")&&plays==before,"unknown emotion stays silent without invented sound");
 character.id=0;character.male=true;
 for(unsigned seed=0;seed<32;++seed){std::srand(seed);const int expected=std::rand()%3;const int remaining=std::rand();std::srand(seed);Event("%!mrad_emotion_joy");Check(lastSound=="weapons/joy"+std::to_string(expected+1)+".wav"&&std::rand()==remaining,"array choice and RNG advancement match original modulo");}
 json=R"({"MRAD_EMOTION_HI":{"man":"weapons/new_hi.wav"}})";
 Event("%!mrad_emotion_hi");Check(lastSound=="weapons/man_hi.wav"&&reads==1,"one load per session while level unchanged");
 ResetEmotionVoices();Event("%!mrad_emotion_hi");Check(lastSound=="weapons/new_hi.wav"&&reads==2,"same-map Reset reloads resources");
 json=R"({"MRAD_EMOTION_HI":{"man":"weapons/level_hi.wav"}})";level="maps/zs_lostcity.bsp";Event("%!mrad_emotion_hi");Check(lastSound=="weapons/level_hi.wav"&&reads==3,"new level reloads resources");
 Setup(R"({"MRAD_EMOTION_HI":{"19s1woman":[],"woman":"weapons/fallback.wav"}})");character.id=101;Event("%!mrad_emotion_hi");Check(lastSound=="weapons/fallback.wav","explicit empty vector falls back safely");
 Setup(R"({"MRAD_EMOTION_HI":{"19s1woman":["weapons/own.wav"],"woman":"weapons/fallback.wav"}})");character.id=101;Event("%!mrad_emotion_hi");Check(lastSound=="weapons/own.wav","nonempty own voice precedes fallback");
 Setup(R"({"MRAD_EMOTION_HI":{"19s1woman":[],"woman":[]}})");character.id=101;Event("%!mrad_emotion_hi");Check(plays==0,"empty fallback vector never divides by zero");
 Setup(R"({"MRAD_EMOTION_HI":{"woman":"weapons/fallback.wav"}})");character.id=118;Event("%!mrad_emotion_hi");Check(plays==0,"voice19 has no original fallback despite character sex");
 Setup(R"({"MRAD_EMOTION_HI":{"buffdefault":["vox/$buffclassname/emotion/hi1.wav","vox/$buffclassname/emotion/hi2.wav"],"woman":"weapons/fallback.wav"}})");character.id=132;
 existing.insert("sound/vox/buffclass22s2tr/emotion/hi2.wav");Event("%!mrad_emotion_hi");Check(lastSound=="vox/buffclass22s2tr/emotion/hi2.wav","template only adds existing exact character files");
 Check(probes==2,"first event probes only requested voice candidates, not all 36 voices");
 Event("%!mrad_emotion_hi");Check(probes==2,"repeat event reuses candidate vector");
 character.id=133;Event("%!mrad_emotion_hi");Check(probes==4&&lastSound=="weapons/fallback.wav","new character resolves only its own candidates");
 Event("%!mrad_emotion_hi");Check(probes==4,"negative template result is cached");
 Event("%!mrad_emotion_absent");Check(probes==4,"absent sentence never scans unrelated templates");
 existing.insert("sound/vox/buffclass22s2ct/emotion/hi1.wav");ResetEmotionVoices();Event("%!mrad_emotion_hi");
 Check(probes==6&&lastSound=="vox/buffclass22s2ct/emotion/hi1.wav","resource reset invalidates negative resolution");
 Setup(R"({"MRAD_EMOTION_HI":{"buffdefault":["vox/$buffclassname/emotion/hi1.wav"],"man":"weapons/fallback.wav"}})");character.id=132;Event("%!mrad_emotion_hi");Check(lastSound=="weapons/fallback.wav","missing template uses original voice33 man fallback");
 Setup("{invalid");Event("%!mrad_emotion_hi");Check(plays==0&&warnings==1,"invalid table is silent and logged");
 Setup("{}");throwRead=true;Event("%!mrad_emotion_hi");Check(plays==0&&warnings==1,"resource load failure is contained");
 Setup(R"({"MRAD_EMOTION_HI":{"man":"weapons/man_hi.wav"}})");engine.pfnPlaySoundByName=nullptr;Check(Event("%!mrad_emotion_hi")&&plays==0,"absent playback function is handled");
 if(argc>1){
  std::ifstream file(argv[1],std::ios::binary);std::string actual((std::istreambuf_iterator<char>(file)),{});Check(!actual.empty(),"real common sentence table loaded");Setup(actual);character.id=114;Event("%!mrad_emotion_hi");Check(lastSound=="weapons/man_hi.wav","real table leechang uses original fallback");
  csol::voice::SentenceTable source;Check(csol::voice::ReadSentences(actual,source,"MRAD_EMOTION"),"real table parses for exhaustive equivalence");
  for(unsigned availability=0;availability<4;++availability){
   auto exists=[&](const std::string& path){unsigned hash=0;for(unsigned char c:path)hash=hash*33+c;return availability==3||(availability&&hash%3==availability-1);};
   auto eager=source;csol::voice::ExpandTemplates(eager,exists);csol::voice::SentenceResolver lazy(source);
   for(const auto& [sentence,voices]:source)for(const auto voice:csol::voice::VoiceNames){
    const auto* expected=csol::voice::FindSentence(eager,sentence,voice);const auto* resolved=lazy.Find(sentence,voice,exists);
    Check((!expected&&!resolved)||(expected&&resolved&&*expected==*resolved),"all real sentences/voices match original eager candidate order and fallback");
    Check(lazy.Find(sentence,voice,[](const std::string&){Check(false,"cached resolution must not reprobe");return false;})==resolved,"repeat resolution preserves result");
   }
  }
 }
 if(argc>2){
  std::ifstream file(argv[2],std::ios::binary);std::string actual((std::istreambuf_iterator<char>(file)),{});rapidjson::Document oracle;oracle.Parse(actual.data(),actual.size());
  Check(!oracle.HasParseError()&&oracle.IsObject(),"original instruction oracle is readable");
  if(!oracle.HasParseError()&&oracle.IsObject()){
   for(const auto& row:oracle["characterVoiceOriginal"].GetArray())Check(csol::voice::CharacterVoiceId(row["model"].GetInt(),row["male"].GetInt()!=0)==row["voiceId"].GetInt(),"character voice agrees with original instruction execution");
   for(const auto& row:oracle["fallbackOriginal"].GetArray())Check(csol::voice::FallbackVoiceId(row["voiceId"].GetInt())==row["fallbackId"].GetInt(),"fallback agrees with original instruction execution");
  }
 }
 std::cout<<"Emotion actual client "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
