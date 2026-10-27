// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/characters.cpp"
#include <iostream>
#include <fstream>
class MouseInput final : public vgui::IInput {
public:
    bool down{};int x{},y{};
    void SetMouseFocus(vgui::VPANEL) override{}
    void SetMouseCapture(vgui::VPANEL) override{}
    void GetKeyCodeText(vgui::KeyCode,char*,int) override{}
    vgui::VPANEL GetFocus() override{return 0;}
    vgui::VPANEL GetMouseOver() override{return 0;}
    void SetCursorPos(int a,int b) override{x=a;y=b;}
    void GetCursorPos(int& a,int& b) override{a=x;b=y;}
    bool WasMousePressed(vgui::MouseCode) override{return false;}
    bool WasMouseDoublePressed(vgui::MouseCode) override{return false;}
    bool IsMouseDown(vgui::MouseCode code) override{return code==vgui::MOUSE_LEFT && down;}
    void SetCursorOveride(vgui::HCursor) override{}
    vgui::HCursor GetCursorOveride() override{return 0;}
    bool WasMouseReleased(vgui::MouseCode) override{return false;}
    bool WasKeyPressed(vgui::KeyCode) override{return false;}
    bool IsKeyDown(vgui::KeyCode) override{return false;}
    bool WasKeyTyped(vgui::KeyCode) override{return false;}
    bool WasKeyReleased(vgui::KeyCode) override{return false;}
    vgui::VPANEL GetAppModalSurface() override{return 0;}
    void SetAppModalSurface(vgui::VPANEL) override{}
    void ReleaseAppModalSurface() override{}
    void GetCursorPosition(int& a,int& b) override{GetCursorPos(a,b);}
};
namespace csol::client {
metahook_api_t* api{};cl_enginefunc_t engine{};cl_exportfuncs_t downstream{};ref_params_t view{};
std::vector<std::byte> ReadResource(const char*){throw std::runtime_error("unexpected resource read");}
void WeaponInventoryCreateMove(float,usercmd_t*,int){}
}
int main(int argc,char** argv)
{
    using namespace csol;using namespace csol::client;int checks=0;
    static int lookups=0,sends=0;static cl_entity_t entity;entity.index=1;
    engine.GetLocalPlayer=[]()->cl_entity_t*{throw std::runtime_error("unsafe loading-screen local entity read");};
    engine.pfnGetLevelName=[]()->const char*{return "";};
    engine.GetEntityByIndex=[](int index)->cl_entity_t*{++lookups;return index==1?&entity:nullptr;};
    engine.GetClientTime=[]()->float{return 1;};engine.pfnServerCmd=[](const char*)->int{++sends;return 1;};
    try {
        CharacterFrame();if(lookups||sends)return 1;++checks;
        engine.pfnGetLevelName=[]()->const char*{return "maps/cs_assault.bsp";};
        CharacterFrame();if(lookups||sends)return 2;++checks;
        view.maxclients=32;view.playernum=0;CharacterFrame();if(lookups!=1||sends!=1)return 3;++checks;
        CharacterFrame();if(lookups!=2||sends!=1)return 4;++checks;
        characters[1]={1,0,0,34};engine.GetClientTime=[]()->float{return 5;};CharacterFrame();if(sends!=1)return 5;++checks;
        engine.pfnGetLevelName=[]()->const char*{return "";};CharacterFrame();if(lookups!=3)return 6;++checks;
        Character character;character.id=168;catalog={character};loaded=true;chosen=168;
        engine.GetLocalPlayer=[]()->cl_entity_t*{return &entity;};
        auto packet=EncodeCharacter({1,1,168,34});visible=true;selectionPending=false;
        Message(CharacterMessage,int(packet.size()),packet.data());if(!visible)return 8;++checks;
        selectionPending=true;pendingId=168;Message(CharacterMessage,int(packet.size()),packet.data());if(visible)return 9;++checks;
        catalog.clear();for(int i=1;i<=24;++i){Character c;c.id=i;c.team=i<=12?1:2;catalog.push_back(c);}
        visible=true;left=100;top=50;scale=1.5f;team=1;page=0;chosen=1;
        MouseInput mouse;input=&mouse;mouse.SetCursorPos(X(20),Y(80));mouse.down=true;
        CharacterFrame();if(chosen!=2)return 10;++checks;
        mouse.SetCursorPos(X(20),Y(105));CharacterFrame();if(chosen!=2)return 11;++checks;
        mouse.down=false;CharacterFrame();mouse.down=true;CharacterFrame();if(chosen!=3)return 12;++checks;
        mouse.down=false;CharacterFrame();mouse.SetCursorPos(X(150),Y(35));mouse.down=true;CharacterFrame();if(team!=2||chosen!=13)return 13;++checks;
        mouse.down=false;CharacterFrame();mouse.SetCursorPos(X(150),Y(320));mouse.down=true;CharacterFrame();if(page!=1)return 14;++checks;
        mouse.down=false;CharacterFrame();mouse.SetCursorPos(X(30),Y(320));mouse.down=true;CharacterFrame();if(page!=0)return 15;++checks;
        auto key=[&](int code){int down=1,result=0;const char* binding=nullptr;VGUI2Extension_CallbackContext context;context.pPluginReturnValue=&result;overlay.Key_Event(down,code,binding,&context);return result==1 && context.Result==VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS;};
        if(!key(149)||page!=1)return 16;++checks;
        if(!key('2')||chosen!=24)return 17;++checks;
        if(!key(150)||page!=0)return 18;++checks;
        if(!key(130)||team!=1||chosen!=1)return 19;++checks;
        if(!key(131)||team!=2||chosen!=13)return 20;++checks;
        mouse.down=false;CharacterFrame();mouse.SetCursorPos(X(400),Y(385));mouse.down=true;
        const int before=sends;CharacterFrame();CharacterFrame();if(sends!=before+1||!selectionPending)return 21;++checks;
        usercmd_t command{};command.buttons=3;command.forwardmove=200;command.sidemove=100;
        engine.SetViewAngles=[](float*){};CharacterCreateMove(.01f,&command,1);if(command.buttons||command.forwardmove||command.sidemove)return 22;++checks;
        if(!key(27)||visible||selectionPending)return 23;++checks;
        if(argc!=2)throw std::runtime_error("supply the generated original menu JSON");
        std::ifstream file(argv[1],std::ios::binary);std::string json((std::istreambuf_iterator<char>(file)),{});
        original=ParseCharacterMenu(json);originalLoaded=true;originalMode=true;visible=true;page=0;team=1;chosen=1;hovered=-1;mouse.down=false;
        auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("original menu check "+std::to_string(checks));};
        check(original.descriptions.size()==151 && original.width==606 && original.height==420);
        const int beforeHover=sends;
        auto r=original.slots[1];Hover(X(r.x+10),Y(r.y+10));check(chosen==2 && sends==beforeHover);
        Hover(X(-10),Y(-10));check(chosen==2 && sends==beforeHover);
        check(key('3') && sends==beforeHover+1 && pendingId==3);
        r=original.slots[4];Hover(X(r.x+10),Y(r.y+10));check(chosen==5 && pendingId==3);
        packet=EncodeCharacter({1,1,3,34});Message(CharacterMessage,int(packet.size()),packet.data());check(!visible && !selectionPending);
        visible=true;const int beforeClick=sends;r=original.slots[5];mouse.SetCursorPos(X(r.x+10),Y(r.y+10));mouse.down=true;
        CharacterFrame();CharacterFrame();check(sends==beforeClick+1 && pendingId==6);
        check(key('=') && page==1 && chosen==11);check(key('=') && page==1);check(key('-') && page==0 && chosen==1);
        for(float factor:{1.f,1.25f,1.5f,2.25f,3.f}){scale=factor;left=13;top=29;r=original.slots[9];
            check(OriginalHit((X(r.x+12)-left)/scale,(Y(r.y+12)-top)/scale)==9);
            check(OriginalHit((X(original.previous.x+10)-left)/scale,(Y(original.previous.y+10)-top)/scale)==12);}
        originalMode=false;chosen=1;selectionPending=false;check(key('2') && chosen==2 && !selectionPending);
        void* window=nullptr;unsigned int msg=WM_ACTIVATEAPP,param=0;long lparam=0;VGUI2Extension_CallbackContext focus;
        overlay.CallEngineSurfaceWndProc(window,msg,param,lparam,&focus);check(!visible);
        visible=true;appearanceMenu=true;
        overlay.CallEngineSurfaceWndProc(window,msg,param,lparam,&focus);check(visible && appearanceMenu && focusSuspended);
        const int beforeSuspended=sends;check(key('1') && sends==beforeSuspended && focusSuspended);
        mouse.SetCursorPos(X(400),Y(385));mouse.down=true;
        param=1;overlay.CallEngineSurfaceWndProc(window,msg,param,lparam,&focus);check(visible && appearanceMenu && !focusSuspended);
        check(lastButtons==1);CharacterFrame();check(sends==beforeSuspended);
        mouse.down=false;CharacterFrame();mouse.down=true;CharacterFrame();check(sends==beforeSuspended+1);
        Close();
        for(const char* invalid:{"{}","[]","{\"version\":2}"}){bool rejected=false;try{ParseCharacterMenu(invalid);}catch(const std::exception&){rejected=true;}check(rejected);}
        // Appearance confirmation may arrive before the first Spawn activates
        // the model. A different player's state must never accept our choice.
        visible=true;appearanceMenu=true;team=1;chosen=1;selectionPending=true;pendingId=1;
        check(key(131) && team==1);
        packet=EncodeCharacter({2,0,1,99});Message(CharacterMessage,int(packet.size()),packet.data());check(visible);
        packet=EncodeCharacter({1,0,1,34});Message(CharacterMessage,int(packet.size()),packet.data());check(!visible);
        visible=true;appearanceMenu=true;const int beforeCancel=sends;check(key(27) && sends==beforeCancel+1 && !appearanceMenu);
        static int nativeVGUI=0,nativeMenus=0;static std::string forwarded;
        previousVGUI=[](const char*,int,void*){++nativeVGUI;return 42;};
        previousShowMenu=[](const char*,int size,void* data){++nativeMenus;auto* b=static_cast<char*>(data);if(size>=5)forwarded+=b+4;return 43;};
        unsigned char vgui[1]{26};serverCharacters=false;
        check(VGUIMenu("VGUIMenu",1,vgui)==42 && nativeVGUI==1);
        serverCharacters=true;check(VGUIMenu("VGUIMenu",1,vgui)==42); // no usable surface: native fallback
        vgui[0]=2;check(VGUIMenu("VGUIMenu",1,vgui)==42); // team menu is untouched
        auto textPacket=[&](std::string text,bool more){std::vector<unsigned char> b{31,0,255,static_cast<unsigned char>(more)};b.insert(b.end(),text.begin(),text.end());b.push_back(0);return b;};
        auto fragment=textPacket("#Terrorist_",true);check(ShowMenu("ShowMenu",int(fragment.size()),fragment.data())==1 && nativeMenus==0);
        fragment=textPacket("Select",false);check(ShowMenu("ShowMenu",int(fragment.size()),fragment.data())==43 && nativeMenus==2 && forwarded=="#Terrorist_Select");
        forwarded.clear();fragment=textPacket("ZP buy menu",false);check(ShowMenu("ShowMenu",int(fragment.size()),fragment.data())==43 && forwarded=="ZP buy menu");
        unsigned char invalidMenu[5]{1,0,255,0,'x'};check(ShowMenu("ShowMenu",4,invalidMenu)==43);
        previousVGUI=nullptr;previousShowMenu=nullptr;
        mouse.down=false;input=nullptr;
        std::cout<<"Character frame lifecycle checks: "<<checks<<" passed\n";
    }catch(const std::exception& e){std::cerr<<e.what();return 7;}
}
