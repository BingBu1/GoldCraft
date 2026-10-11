#include "server_hud.hpp"
#include "goldcraft/death_notice.hpp"
#include <cvardef.h>
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>

namespace goldcraft::server_hud {
namespace {
cl_enginefunc_t* engine=nullptr;
struct Color {int r,g,b;};
constexpr Color white{255,255,255},yellow{255,204,64};
Color team_color(unsigned team){return team==1?Color{255,64,64}:team==2?Color{153,204,255}:white;}
Color menu_color(char color){
    switch(color){case 'r':return {255,64,64};case 'y':return yellow;case 'd':return {140,140,140};case 'g':return {64,255,64};default:return white;}
}
struct Icon {std::string name;int sprite=0;wrect_t rect{};};
struct Notice {
    std::string killer,victim,weapon;
    Color killer_color=white,victim_color=white;
    bool headshot=false;
    double expires=0;
    int killer_width=0,victim_width=0;
};
struct Text {std::string text;Color color;int x=0,y=0;};
std::array<unsigned,33> teams{};
std::array<Notice,4> feed{};
unsigned count=0;
std::vector<Icon> icons;
std::vector<Text> menu_text;
std::uint64_t messages=0,draws=0,menus=0,menu_revision=0;
int screen_width=0,screen_height=0,font_height=16,panel_width=0,panel_height=0,panel_y=0;
cvar_t* duration=nullptr;

std::string name(unsigned slot){
    if(!slot)return {};
    hud_player_info_t player{};engine->pfnGetPlayerInfo(static_cast<int>(slot),&player);
    std::string result;
    if(player.name)for(unsigned i=0;i<128&&player.name[i];++i){
        const auto c=static_cast<unsigned char>(player.name[i]);if(c>=32)result+=static_cast<char>(c);
    }
    return result;
}
int measure(const std::string& text){
    int width=0,height=0;engine->pfnDrawConsoleStringLen(text.c_str(),&width,&height);
    font_height=std::max(font_height,height);return width;
}
void text(int x,int y,const std::string& value,Color color){
    engine->pfnDrawSetTextColor(color.r/255.f,color.g/255.f,color.b/255.f);
    engine->pfnDrawConsoleString(x,y,const_cast<char*>(value.c_str()));
}
const Icon* icon(std::string_view name){
    for(const auto& value:icons)if(value.name==name)return &value;
    return nullptr;
}
int icon_width(const Icon* value){return value?value->rect.right-value->rect.left:0;}
int sprite(int x,int y,const Icon* value,Color color){
    if(!value)return 0;
    engine->pfnSPR_Set(value->sprite,color.r,color.g,color.b);
    engine->pfnSPR_DrawAdditive(0,x,y,&value->rect);return icon_width(value);
}
void expire(double now){
    unsigned out=0;
    for(unsigned i=0;i<count;++i)if(now<feed[i].expires){if(out!=i)feed[out]=std::move(feed[i]);++out;}
    count=out;
}
void layout_menu(const FormMenu& menu){
    menu_text.clear();panel_width=240;
    for(const auto& line:menu.lines()){
        int width=0;for(const auto& run:line)width+=measure(run.text);
        panel_width=std::max(panel_width,width+32);
    }
    panel_width=std::min(panel_width,screen_width-32);
    const int step=font_height+4;
    panel_height=std::min(static_cast<int>(menu.lines().size())*step+24,screen_height-32);
    panel_y=(screen_height-panel_height)/2;
    int y=panel_y+12;
    for(const auto& line:menu.lines()){
        if(y+font_height>panel_y+panel_height-8)break;
        int x=28,right_width=0;bool right=false;
        for(const auto& run:line)if(run.right)right_width+=measure(run.text);
        for(const auto& run:line){
            if(run.right&&!right){x=16+panel_width-12-right_width;right=true;}
            menu_text.push_back({run.text,menu_color(run.color),x,y});x+=measure(run.text);
        }
        y+=step;
    }
    menu_revision=menu.revision();
}
void video(int width,int height){
    screen_width=width;screen_height=height;font_height=16;icons.clear();menu_revision=0;
    int total=0;auto* list=engine->pfnSPR_GetList(const_cast<char*>("sprites/hud.txt"),&total);
    const int resolution=width<640?320:640;
    for(int i=0;list&&i<total;++i){
        const auto& item=list[i];
        if(item.iRes!=resolution||std::strncmp(item.szName,"d_",2)!=0)continue;
        const std::string path="sprites/"+std::string(item.szSprite)+".spr";
        const auto handle=engine->pfnSPR_Load(path.c_str());
        if(handle)icons.push_back({item.szName,handle,item.rc});
    }
    for(unsigned i=0;i<count;++i){feed[i].killer_width=measure(feed[i].killer);feed[i].victim_width=measure(feed[i].victim);}
}
}
void initialize(cl_enginefunc_t* value){engine=value;duration=engine->pfnGetCvarPointer("hud_deathnotice_time");}
void reset(){teams.fill(0);count=0;menu_text.clear();menu_revision=0;}
void video_reset(){icons.clear();screen_width=screen_height=0;menu_revision=0;}
void death(std::span<const std::uint8_t> message,double now){
    const auto event=read_death_notice(message);if(!engine)return;
    expire(now);
    if(count==feed.size()){for(unsigned i=1;i<count;++i)feed[i-1]=std::move(feed[i]);--count;}
    auto& notice=feed[count++];notice={};
    notice.killer=event.killer==event.victim?std::string{}:name(event.killer);notice.victim=name(event.victim);
    notice.killer_color=team_color(teams[event.killer]);notice.victim_color=team_color(teams[event.victim]);
    notice.headshot=event.headshot;notice.weapon="d_"+event.weapon;
    const double seconds=duration&&std::isfinite(duration->value)?std::clamp(static_cast<double>(duration->value),0.0,60.0):6.0;
    notice.expires=now+seconds;notice.killer_width=measure(notice.killer);notice.victim_width=measure(notice.victim);++messages;
}
void team(std::span<const std::uint8_t> message){
    if(message.size()<2||message.size()>34||message[0]>32||message.back()!=0)return;
    const auto end=std::find(message.begin()+1,message.end(),0);
    const std::string_view value(reinterpret_cast<const char*>(message.data()+1),static_cast<std::size_t>(end-message.begin()-1));
    teams[message[0]]=value=="TERRORIST"?1:value=="CT"?2:0;
}
void draw(double now,const FormMenu& menu,bool show_menu){
    if(!engine)return;
    SCREENINFO screen{};screen.iSize=sizeof(screen);engine->pfnGetScreenInfo(&screen);
    if(screen.iWidth<320||screen.iHeight<240)return;
    if(screen_width!=screen.iWidth||screen_height!=screen.iHeight)video(screen.iWidth,screen.iHeight);
    expire(now);int y=32;
    for(unsigned i=0;i<count;++i){
        const auto& notice=feed[i];const auto* weapon=icon(notice.weapon);if(!weapon)weapon=icon("d_skull");
        const auto* head=notice.headshot?icon("d_headshot"):nullptr;
        const int width=notice.killer_width+notice.victim_width+icon_width(weapon)+icon_width(head)+12;
        int x=std::max(8,screen_width-12-width);
        text(x,y,notice.killer,notice.killer_color);x+=notice.killer_width+4;
        x+=sprite(x,y,weapon,yellow);x+=sprite(x,y,head,yellow);x+=4;
        text(x,y,notice.victim,notice.victim_color);y+=std::max(font_height,16)+4;++draws;
    }
    if(show_menu&&menu.active(now)){
        if(menu_revision!=menu.revision())layout_menu(menu);
        engine->pfnFillRGBA(16,panel_y,panel_width,panel_height,12,17,23,210);
        for(const auto& run:menu_text)text(run.x,run.y,run.text,run.color);
        ++menus;
    }
    engine->pfnDrawSetTextColor(1,1,1);
}
std::uint64_t death_messages(){return messages;}
std::uint64_t death_draws(){return draws;}
std::uint64_t menu_draws(){return menus;}
unsigned notices(double now){expire(now);return count;}
}
