// SPDX-License-Identifier: GPL-3.0-or-later
// Complete production translation unit, pinned SDK and real upstream controls.
// Engine/VGUI boundaries are controlled; this is not a visual or gameplay test.
#include <windows.h>
static HWND BuyFixtureForeground(){return reinterpret_cast<HWND>(1);}
static DWORD BuyFixtureWindowProcess(HWND,DWORD* process){*process=GetCurrentProcessId();return 1;}
#define GetForegroundWindow BuyFixtureForeground
#define GetWindowThreadProcessId BuyFixtureWindowProcess
#include "tests/csol-hud-style/allocation_probe.hpp"
#include "native/csol/client/buy_menu.cpp"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#include "tests/csol-hud-style/surface_stub.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <source_location>
#include <tuple>
#include <type_traits>
#include "native_panel.hpp"
#include <vgui_controls/EditablePanel.h>

vgui::IInput2* g_pVGuiInput2{};
vgui::ISurface2* g_pVGuiSurface2{};
vgui::ISchemeManager2* g_pVGuiSchemeManager2{};
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return nullptr;}

namespace fixture {
using namespace csol;
using namespace csol::buy;
using namespace csol::client;
unsigned checks{},failures{},reads{},uploads{},deletes{},syncs{},clears{},activates{},deactivates{};
unsigned nativeBuys{},nativeAutoBuys{},nativeDrops{};
bool observePurchase{},dispatchVisible{};
double time{};
std::filesystem::path resources;
std::vector<std::string> commands;
std::array<cvar_t,2> cvars{};
std::array<std::string,2> favoriteStrings;
void Check(bool value,const char* text,std::source_location at=std::source_location::current())
{++checks;if(!value){++failures;std::cerr<<"FAIL "<<at.line()<<' '<<text<<'\n';}}
void Log(const char*,...){}
struct Surface final:vgui::HudTestSurface {
 int next=10,w=1280,h=720;std::set<int> live;vgui::VPANEL embedded{1};
 struct Image {int texture,x0,y0,x1,y1;};std::vector<Image> images;int selectedTexture{};bool recordImages{};
 vgui::VPANEL GetEmbeddedPanel()override{return embedded;}
 void GetScreenSize(int& x,int& y)override{x=w;y=h;}
 int CreateNewTextureID(bool)override{live.insert(++next);return next;}
 void DrawSetTextureRGBA(int id,const unsigned char*,int,int,int,bool)override{++uploads;Check(live.contains(id),"upload owns real surface texture ID");}
 bool DeleteTextureByID(int id)override{++deletes;Check(live.erase(id)==1,"texture deleted once");return true;}
 void GetTextSize(vgui::HFont,const wchar_t* text,int& x,int& y)override{x=int(std::wcslen(text))*7;y=13;}
 int GetFontTall(vgui::HFont)override{return 13;}
 void GetProportionalBase(int& x,int& y)override{x=640;y=480;}
 void DrawSetTexture(int id)override{selectedTexture=id;}
 void DrawTexturedRect(int x0,int y0,int x1,int y1)override{if(recordImages)images.push_back({selectedTexture,x0,y0,x1,y1});}
} capture;
struct Input final:vgui::IInput {
 bool left{},right{};int x=-1,y=-1;vgui::VPANEL captured{};unsigned captureCalls{};
 void SetMouseFocus(vgui::VPANEL)override{}void SetMouseCapture(vgui::VPANEL p)override{captured=p;++captureCalls;}
 void GetKeyCodeText(vgui::KeyCode,char*,int)override{}
 vgui::VPANEL GetFocus()override{return 0;}vgui::VPANEL GetMouseOver()override{return 0;}
 void SetCursorPos(int a,int b)override{x=a;y=b;}void GetCursorPos(int& a,int& b)override{a=x;b=y;}
 bool WasMousePressed(vgui::MouseCode)override{return false;}bool WasMouseDoublePressed(vgui::MouseCode)override{return false;}
 bool IsMouseDown(vgui::MouseCode c)override{return c==vgui::MOUSE_LEFT?left:c==vgui::MOUSE_RIGHT?right:false;}
 void SetCursorOveride(vgui::HCursor)override{}vgui::HCursor GetCursorOveride()override{return 0;}
 bool WasMouseReleased(vgui::MouseCode)override{return false;}bool WasKeyPressed(vgui::KeyCode)override{return false;}
 bool IsKeyDown(vgui::KeyCode)override{return false;}bool WasKeyTyped(vgui::KeyCode)override{return false;}bool WasKeyReleased(vgui::KeyCode)override{return false;}
 vgui::VPANEL GetAppModalSurface()override{return 0;}void SetAppModalSurface(vgui::VPANEL)override{}void ReleaseAppModalSurface()override{}
 void GetCursorPosition(int& a,int& b)override{GetCursorPos(a,b);}
} inputCapture;
void Setup(){
 engine.Con_Printf=engine.Con_DPrintf=Log;
 engine.GetClientTime=[](){return float(time);};
 engine.Con_IsVisible=[](){return 0;};
 engine.GetViewAngles=[](float* out){out[0]=1;out[1]=2;out[2]=3;};engine.SetViewAngles=[](float*){};
 engine.pfnServerCmd=[](const char* text){if(observePurchase&&(!std::strncmp(text,"csol_buy ",9)||!std::strcmp(text,"rebuy_in\n")))dispatchVisible=visible;commands.emplace_back(text);return 1;};
 engine.pfnClientCmd=[](const char*){return 1;};
 engine.Cvar_Set=[](const char* name,const char* text){const int i=!std::strcmp(name,"ct");favoriteStrings[i]=text;cvars[i].string=favoriteStrings[i].data();};
 downstream.IN_ClearStates=[](){++clears;};downstream.IN_ActivateMouse=[](){++activates;};downstream.IN_DeactivateMouse=[](){++deactivates;};
 surface=&capture;input=&inputCapture;schemes=nullptr;extension=reinterpret_cast<IVGUI2Extension*>(1);
 for(int i=0;i<2;++i){cvars[i].name=const_cast<char*>(i?"ct":"t");favoriteStrings[i]="0,0,0,0,0,0,0,0";cvars[i].string=favoriteStrings[i].data();favoriteCvars[i]=&cvars[i];}
 view.health=100;view.spectator=0;
}
void Deliver(Kind kind,int id=0,Result result=Result::Available,int req=-1,int money=32000,int price=100){
 auto bytes=Encode({kind,req<0?request:std::uint16_t(req),std::uint16_t(id),price,money,result,2});
 Check(Receive(Message,int(bytes.size()),bytes.data())==1,"real protocol consumer accepts encoded packet");
}
void Offers(){Deliver(Kind::Begin);for(const auto& item:Catalog())Deliver(Kind::Item,item.id);Deliver(Kind::End);}
void Fresh(){
 Close(false);submittedPurchases={};observePurchase=dispatchVisible=false;visible=true;capability=true;catalogReady=false;request=7;requestedAt=time=0;balance=32000;
 category=-1;page=0;hover=-1;detailItem=0;query.clear();rowsCategory=-2;commands.clear();offers.clear();status.clear();leftInput.Reset();rightInput.Reset();swallowMouse=restoreMouse=false;inputCapture.left=inputCapture.right=false;Offers();commands.clear();
}
void Key(int key,bool down){int d=down,k=key,value{};const char* binding=nullptr;VGUI2Extension_CallbackContext context{};context.pPluginReturnValue=&value;overlay.Key_Event(d,k,binding,&context);Check(value==1,"real overlay consumes menu input");}
void StateTests(){
 Fresh();visible=false;capability=false;oldBuy=[](){++nativeBuys;};const auto nativeBefore=nativeBuys;OpenMenu(true);Check(!visible&&nativeBuys==nativeBefore+1,"ordinary server falls back through chained native buy");
 Deliver(Kind::Capability);OpenMenu(false);Check(visible&&!catalogReady&&request==8&&commands.size()==1,"capable open requests authoritative catalog");OpenMenu(false);Check(!visible,"buy toggle closes visible menu");
 unsigned char malformed[1]{};const auto rejectedBefore=rejected;Check(Receive(Message,1,malformed)==0&&rejected==rejectedBefore+1,"malformed protocol rejected");
 Fresh();const auto* item=Find(22);Check(item!=nullptr,"catalog lookup actual M4A1");
 const auto before=purchases;observePurchase=true;Purchase(*item);observePurchase=false;
 Check(dispatchVisible&&!visible&&commands.size()==1,"ordinary send precedes same-stack close without reply wait");
 Check(commands[0]=="csol_buy 7 22\n"&&purchases==before,"ordinary dispatch does not invent authoritative success");
 Purchase(*item);Purchase(*Find(16));Check(commands.size()==1,"closed menu cannot dispatch extra direct purchases");
 time=100;BuyMenuFrame();Deliver(Kind::Result,22,Result::Success,7,123);
 Check(!visible&&purchases==before+1,"delayed ordinary authority success is recorded after local close");
 const auto closedBalance=balance;Deliver(Kind::Result,22,Result::Success,7,5);
 Check(purchases==before+1&&balance==closedBalance,"duplicate detached success neither recounts nor updates UI balance");
 // Compare current UI/transaction state around detached or invalid receipts.
 // Legitimate ordinary success updates only detached authority accounting.
 const auto ui=[](){return std::tuple{visible,catalogReady,request,balance,balanceText,status,category,page,query,offers.size(),commands.size()};};
 Fresh();Purchase(*item);OpenMenu(false);Offers();category=4;page=1;query=L"new search";status=L"new menu state";
 const auto freshUI=ui();const auto freshCount=purchases;
 Deliver(Kind::Result,16,Result::Success,7,1);Check(ui()==freshUI&&purchases==freshCount,"wrong item cannot consume detached ordinary token");
 Deliver(Kind::Result,22,Result::Success,999,2);Check(ui()==freshUI&&purchases==freshCount,"wrong token cannot acknowledge ordinary purchase");
 Deliver(Kind::Result,22,Result::Available,7,3);Check(ui()==freshUI&&purchases==freshCount,"availability packet is not ordinary completion");
 Deliver(Kind::Result,22,Result::Success,7,4);
 Check(ui()==freshUI&&purchases==freshCount+1,"old ordinary ack preserves reopened UI catalog balance query and status");
 Deliver(Kind::Result,22,Result::Success,7,5);Check(ui()==freshUI&&purchases==freshCount+1,"duplicate ordinary ack cannot close reopened UI");
 Deliver(Kind::Result,16,Result::Success,request,6);Check(ui()==freshUI&&purchases==freshCount+1,"unrequested current-token result cannot mutate UI or history");
 for(const auto failure:{Result::Insufficient,Result::Failed,Result::Unavailable}){
   Fresh();Purchase(*item);OpenMenu(false);Offers();status=L"unrelated menu";const auto failureUI=ui();const auto count=purchases;
   Deliver(Kind::Result,22,failure,7,7);Check(ui()==failureUI&&purchases==count,"detached rejection consumes request without changing reopened UI");
   Deliver(Kind::Result,22,Result::Success,7,8);Check(ui()==failureUI&&purchases==count,"success after completed rejection is ignored");
 }
 Fresh();const auto rapidBefore=purchases;Purchase(*item);OpenMenu(false);Offers();Purchase(*Find(16));OpenMenu(false);Offers();
 Check(visible&&request==9&&commands.size()==4,"rapid reopen dispatches independent purchases without waiting gate");
 const auto rapidUI=ui();Deliver(Kind::Result,16,Result::Success,8,9);Deliver(Kind::Result,22,Result::Success,7,10);
 Check(ui()==rapidUI&&purchases==rapidBefore+2,"out-of-order ordinary acknowledgements preserve both purchases and current menu");
 Fresh();balance=0;Purchase(*item);Check(commands.empty(),"insufficient cached balance sends nothing");
 Fresh();offers[item->id].result=Result::Unavailable;Purchase(*item);Check(commands.empty(),"unavailable offer sends nothing");
 Fresh();Refresh();Check(!catalogReady&&offers.empty()&&request==8&&commands.size()==1,"fresh request clears previous offers");
 Deliver(Kind::End,0,Result::Available,7);Check(!catalogReady,"stale catalog end ignored");
 time=6;Check(ExpireRequest()&&!catalogReady&&visible,"timed out request remains visibly unavailable");
 Fresh();request=65534;Purchase(*item);OpenMenu(false);Offers();Purchase(*Find(16));OpenMenu(false);Offers();
 const auto wrapUI=ui();const auto wrapCount=purchases;
 Check(request==1,"request wrap skips zero after maximum-token purchase");
 Deliver(Kind::Result,22,Result::Success,65534,11);Deliver(Kind::Result,16,Result::Success,65535,12);
 Check(ui()==wrapUI&&purchases==wrapCount+2,"high-end token acknowledgements remain independent across wrap");
 Fresh();request=1;Purchase(*item);request=65535;OpenMenu(false);Offers();const auto reuseUI=ui();const auto reuseCount=purchases;
 Deliver(Kind::Result,22,Result::Success,1,13);Check(ui()==reuseUI&&purchases==reuseCount,"reused token retires old request before new submission");
 Purchase(*Find(16));Deliver(Kind::Result,22,Result::Success,1,14);Check(purchases==reuseCount,"wrong old item cannot acknowledge a reused token");
 Deliver(Kind::Result,16,Result::Success,1,15);Check(purchases==reuseCount+1,"reused token accepts its newly submitted matching item");
 Fresh();Purchase(*item);OpenMenu(false);Offers();Purchase(*Find(16));const auto resetCount=purchases;ResetBuyMenu();
 Check(!capability&&!visible&&request==0,"session reset retires UI ownership and detached purchase tokens");
 Deliver(Kind::Result,22,Result::Success,7,16);Deliver(Kind::Result,16,Result::Success,8,17);
 Check(purchases==resetCount&&!visible&&!capability,"session reset discards all detached old acknowledgements");Check(Load(),"resources reload after real session reset");
 Fresh();category=4;Rows();Layout();const auto r=PixelBounds(presentation.slots[0]);inputCapture.x=r.x+r.w/2;inputCapture.y=r.y+r.h/2;
 Key(241,true);Check(commands.empty(),"mouse press does not buy");inputCapture.left=true;BuyMenuFrame();Key(241,false);inputCapture.left=false;BuyMenuFrame();Check(commands.size()==1,"event plus polling release purchases exactly once");
 Fresh();usercmd_t cmd{};cmd.buttons=IN_ATTACK|IN_ATTACK2;cmd.forwardmove=123;cmd.sidemove=234;cmd.upmove=345;BuyMenuCreateMove(&cmd,1);Check(!cmd.buttons&&!cmd.forwardmove&&!cmd.sidemove&&!cmd.upmove&&cmd.viewangles[2]==angles[2],"real create move freezes menu gameplay input");
 Close();cmd.buttons=IN_ATTACK|IN_ATTACK2;inputCapture.left=true;BuyMenuCreateMove(&cmd,1);Check(cmd.buttons==0&&swallowMouse,"close swallows held attacks");inputCapture.left=false;BuyMenuCreateMove(&cmd,1);Check(!swallowMouse,"release ends swallow state");
 Fresh();view.health=0;BuyMenuFrame();Check(!visible,"death closes menu");view.health=100;
 Fresh();category=4;const auto& favoritesRows=Rows();const auto expected=favoritesRows.front()->id;EditFavorite(0);Check(favorites[0]==expected,"actual favorite editor saves visible item");EditFavorite(30);Check(favorites[0]==0,"actual favorite editor removes saved slot");
 Fresh();const auto unsolicitedUI=ui();const auto countBefore=purchases;Deliver(Kind::Result,16,Result::Success);
 Check(ui()==unsolicitedUI&&purchases==countBefore,"unrequested result leaves ordinary UI and authority accounting untouched");
}
void RebuyTests(){
 // The registered command wrapper is tested directly; a physical F2 binding
 // and original special HW state are separate runtime/evidence boundaries.
 ResetBuyMenu();capability=true;commands.clear();time=0;
 const auto beforeReads=reads,beforeUploads=uploads;
 Rebuy();
 Check(commands==std::vector<std::string>{"cl_rebuy\n"}&&!visible&&!loaded&&!catalogReady&&request==0&&offers.empty(),"F2 command needs no local history catalog resources or open menu");
 Check(reads==beforeReads&&uploads==beforeUploads,"F2 command performs no resource reads or uploads");
 for(int i=0;i<16;++i)Rebuy();
 Check(commands.size()==17&&std::all_of(commands.begin(),commands.end(),[](const auto& command){return command=="cl_rebuy\n";}),
  "user-requested no interval forwards every same-time F2 command");
 time=-1;Rebuy();Check(commands.size()==18,"backward client time cannot suppress F2 dispatch");
 view.health=0;Rebuy();Check(commands.size()==19,"F2 forwards death-state request for server authority rather than inventing client gate");
 view.health=100;view.spectator=1;Rebuy();Check(commands.size()==20,"F2 forwards spectator-state request for server authority");view.spectator=0;
 ResetBuyMenu();capability=true;commands.clear();time=0;Rebuy();
 Check(commands==std::vector<std::string>{"cl_rebuy\n"},"host session reset retains immediate F2 command dispatch");
 Check(Load(),"resources reload after F2 reset cases");
 Fresh();capability=false;visible=false;
 // A non-capturing callback is required by the real command ABI.
 oldRebuy=[](){++nativeBuys;};const auto originalBefore=nativeBuys;time=3;Rebuy();
 Check(nativeBuys==originalBefore+1&&commands.empty()&&!visible,"ordinary server F2 calls chained original command");
 oldRebuy=nullptr;time=4;Rebuy();
 Check(commands==std::vector<std::string>{"rebuy\n"}&&!visible,"ordinary server missing original callback sends native rebuy command");
 Fresh();time=10;const auto ui=[](){return std::tuple{visible,catalogReady,request,balance,balanceText,status,category,page,query,offers.size()};};
 const auto beforeUI=ui();Rebuy();Check(commands==std::vector<std::string>{"cl_rebuy\n"}&&ui()==beforeUI,"F2 sends once without acquiring or closing an existing menu");
 commands.clear();observePurchase=true;dispatchVisible=false;Click(41);observePurchase=false;
 Check(dispatchVisible&&!visible&&commands==std::vector<std::string>{"rebuy_in\n"},"menu rebuy follows F2 at the same time then closes in the same stack");
 Fresh();catalogReady=false;StartRebuy();Check(commands==std::vector<std::string>{"rebuy_in\n"}&&!visible,"visible menu rebuy does not require a client catalog");
 commands.clear();StartRebuy();Check(commands.empty(),"closed menu cannot send an additional menu rebuy");
 Fresh();Purchase(*Find(22));const auto ordinaryRequest=request;const auto purchaseBefore=purchases;
 time=20;Rebuy();Check(commands.size()==2&&commands.back()=="cl_rebuy\n"&&!visible,"F2 does not wait for detached ordinary purchase authority");
 const auto rebuyCommands=commands;Deliver(Kind::Result,22,Result::Success,ordinaryRequest,2);
 Check(purchases==purchaseBefore+1&&commands==rebuyCommands&&!visible,"ordinary authority result does not restart or advance an F2 queue");
 Deliver(Kind::Result,22,Result::Success,ordinaryRequest,1);Check(purchases==purchaseBefore+1&&commands==rebuyCommands,"duplicate ordinary result remains isolated from F2 command");
 Fresh();time=30;Rebuy();const auto unsolicitedBefore=purchases;const auto f2UI=ui();
 Deliver(Kind::Result,16,Result::Success);Check(purchases==unsolicitedBefore&&ui()==f2UI&&commands.size()==1,"F2 creates no fake pending per-item result entitlement");
 ResetBuyMenu();Check(Load(),"resources reload before immediate command entry tests");Fresh();visible=false;commands.clear();
 const auto token=request;time=0;Rebuy();Open();
 Check(visible&&request==token+1&&commands==std::vector<std::string>({"cl_rebuy\n","csol_buy_menu "+std::to_string(token+1)+"\n"}),
  "same-time F2 does not suppress ordinary buy opening or catalog dispatch");
 Close();Open();Rebuy();
 Check(visible&&request==token+2&&commands.size()==4&&commands.back()=="cl_rebuy\n",
  "same-time close reopen and following F2 dispatch immediately");
 capability=false;visible=false;const auto buyBefore=nativeBuys;commands.clear();
 oldBuy=[](){++nativeBuys;};oldRebuy=[](){++nativeBuys;};
 for(int i=0;i<2;++i){Open();Rebuy();}
 Check(nativeBuys==buyBefore+4&&commands.empty(),"ordinary-server native buy and rebuy callbacks preserve repeated same-time input");
 oldRebuy=nullptr;
}
void RefreshTests(){
 Fresh();font=11;titleFont=12;tabFont=13;fontRecipeTall=13;titleRecipeTall=18;tabRecipeTall=13;privateFont=reinterpret_cast<HANDLE>(1);
 ResourceFontPreparation prepared;PrepareBuyResourceFonts(prepared);Check(prepared.sources.size()==1&&prepared.recipes.size()==3,"refresh ON prepares body title tab font recipes from current resource");
 const auto caps=capability;const auto textureCount=capture.live.size();InvalidateBuyResources();
 Check(!visible&&!loaded&&presentation.images.empty()&&textures.empty()&&imageRefs.empty()&&capture.live.empty(),"resource invalidation retires all presentation textures");
 Check(capability==caps&&textureCount>0,"resource invalidation preserves session capability");
 Check(Load(),"real resource reload succeeds after invalidation");privateFont=nullptr;
}
void HotPath(){
 Fresh();font=11;titleFont=12;tabFont=13;closeFont=14;category=4;Rows();Layout();Paint();
 const auto readBefore=reads,uploadBefore=uploads;allocation_probe::calls=0;allocation_probe::active=true;for(int i=0;i<100;++i)Paint();allocation_probe::active=false;
 std::cout<<"hotpath paint100 allocations="<<allocation_probe::calls<<" reads="<<reads-readBefore<<" uploads="<<uploads-uploadBefore<<'\n';
 Check(reads==readBefore&&uploads==uploadBefore,"warm paint performs no resource reads or texture uploads");
 allocation_probe::calls=0;allocation_probe::active=true;for(int i=0;i<100;++i)Text(0,0,L"x","brightcontroltext");allocation_probe::active=false;
 std::cout<<"hotpath brightcolor100 allocations="<<allocation_probe::calls<<'\n';
 allocation_probe::calls=0;allocation_probe::active=true;for(int i=0;i<100;++i)BuyMenuFrame();allocation_probe::active=false;
 std::cout<<"hotpath frame100 allocations="<<allocation_probe::calls<<" reads="<<reads-readBefore<<" uploads="<<uploads-uploadBefore<<'\n';
 const auto drawBefore=draws;resource_refresh_detail::readsAllowed=false;int x{},y{},w{},h{};VGUI2Extension_CallbackContext context{};context.IsPost=true;overlay.Paint(x,y,w,h,&context);resource_refresh_detail::readsAllowed=true;Check(draws==drawBefore,"refresh read gate blocks actual overlay painting");
}
void NativeControls(){
 using namespace buy_native;
 Host host;Gui gui(host);Keys keys;buy_native::Input nativeInput;Schemes nativeSchemes;Localize localize;vgui::SystemBoundary system;
 buy_native::Surface nativeSurface(host,gui,capture.w,capture.h);
 g_pVGui=&gui;g_pVGuiPanel=&host;g_pKeyValuesSystem=&keys;g_pVGuiInput2=&nativeInput;g_pVGuiSurface2=&nativeSurface;
 g_pVGuiSchemeManager2=&nativeSchemes;g_pVGuiSystem=&system;g_pVGuiLocalize=&localize;
 struct Cleanup {~Cleanup(){
   menuLayer.reset();detailPanel=nullptr;descriptionPanel=nullptr;searchEntry=nullptr;
   g_pVGui=nullptr;g_pVGuiPanel=nullptr;g_pKeyValuesSystem=nullptr;g_pVGuiInput2=nullptr;g_pVGuiSurface2=nullptr;
   g_pVGuiSchemeManager2=nullptr;g_pVGuiSystem=nullptr;g_pVGuiLocalize=nullptr;
 }} cleanup;
 Fresh();font=11;titleFont=12;tabFont=13;closeFont=14;category=4;Rows();Layout();
 const auto coldReads=reads,coldUploads=uploads;allocation_probe::calls=0;allocation_probe::active=true;
 PrepareDetailControls();UpdateDetail();allocation_probe::active=false;
 std::cout<<"native cold construction allocations="<<allocation_probe::calls<<" reads="<<reads-coldReads<<" uploads="<<uploads-coldUploads<<'\n';
 Check(reads==coldReads&&uploads==coldUploads,"native cold construction reuses prepared resources");
 Check(menuLayer&&detailPanel&&descriptionPanel,"construct actual native buy controls");
 Check(!detailPanel->IsVisible()&&!descriptionPanel->IsVisible(),"native detail initially hidden before any valid hover");
 int textItem=0,tableItem=0;size_t longest=0;
 for(const auto& [id,info]:presentation.weaponInfo.items){
   if(info.table&&!*info.table){const auto found=infoItems.find(id);if(found!=infoItems.end()&&found->second.description.size()>longest){textItem=id;longest=found->second.description.size();}}
   else if(info.table&&*info.table&&!tableItem)tableItem=id;
 }
 Check(textItem&&tableItem,"real fixture supplies both description and table weapons");
 // The buy05 resource subset's longest real text is short. Retain it as the
 // input sentence and repeat it only in this controlled overflow boundary.
 const auto originalDescription=infoItems.at(textItem).description;
 for(int i=0;i<20;++i)infoItems.at(textItem).description+=L"\n"+originalDescription;
 std::cout<<"native controlled overflow sourceChars="<<longest<<" inputChars="<<infoItems.at(textItem).description.size()<<'\n';
 category=Find(textItem)->category;rowsCategory=-2;const auto& textRows=Rows();
 const auto textRow=std::find_if(textRows.begin(),textRows.end(),[&](const auto* item){return item->id==textItem;});
 Check(textRow!=textRows.end(),"description weapon belongs to actual visible catalog");
 const auto textIndex=int(textRow-textRows.begin());page=textIndex/9;Layout();
 const auto textSlot=PixelBounds(presentation.slots[textIndex%9]);
 inputCapture.x=textSlot.x+textSlot.w/2;inputCapture.y=textSlot.y+textSlot.h/2;
 BuyMenuFrame();gui.RunFrame();
 Check(hover==textIndex%9&&detailItem==textItem,"actual cursor hit and controller hover select native description");
 Check(detailPanel->IsVisible()&&descriptionPanel->IsVisible(),"actual native description is visible for explicit text mode");
 const auto root=menuLayer->GetVPanel(),detail=detailPanel->GetVPanel(),desc=descriptionPanel->GetVPanel();
 const auto canvas=host.Named(desc,"ScrollableCanvas"),rich=host.Named(desc,"DescText"),bar=host.Named(desc,"WeaponInfoScrollBar");
 Check(canvas&&rich&&bar,"find actual SDK canvas RichText and original-style scrollbar");
 Check(host.GetParent(detail)==root&&host.GetParent(desc)==detail&&host.GetParent(canvas)==desc&&host.GetParent(rich)==canvas,"actual native detail hierarchy preserves all parents");
 Check(host.IsMouseInputEnabled(desc)&&host.IsMouseInputEnabled(bar)&&!host.IsMouseInputEnabled(canvas)&&!host.IsMouseInputEnabled(rich),"scroll owner accepts input while text canvas remains passive");
 auto* actualText=dynamic_cast<vgui::RichText*>(host.Client(rich));
 auto* actualBar=dynamic_cast<vgui::ScrollBar*>(host.Client(bar));
 Check(actualText&&actualBar,"controls are real SDK instances");
 nativeSurface.SolveTraverse(root,true);
 nativeSurface.record=true;menuLayer->PaintOverlay();nativeSurface.record=false;
 Check(nativeSurface.stack.empty()&&nativeSurface.pushes>0,"actual controls paint with balanced native panel contexts");
 Check(actualBar->GetWide()==12,"native description scrollbar retains literal original width");
 const int fullHeight=actualText->GetTall();
 Check(fullHeight>0&&host.nodes[canvas].h==fullHeight,"real RichText computed height owns canvas height");
 std::cout<<"native description item="<<textItem<<" chars="<<longest<<" contentHeight="<<fullHeight<<" viewport="<<descriptionPanel->GetTall()<<'\n';
 Check(actualBar->IsVisible()&&fullHeight>descriptionPanel->GetTall(),"real long description exposes native scrolling");
 std::array<int,4> richClip{};host.GetClipRect(rich,richClip[0],richClip[1],richClip[2],richClip[3]);
 unsigned richDraws=0;
 for(const auto& draw:nativeSurface.text)if(draw.panel==rich){++richDraws;Check(draw.clip==richClip,"actual RichText draw consumes its native ancestor clip");}
 Check(richDraws>0,"actual RichText Paint emitted text under the canvas clip");
 Check(richClip[3]-richClip[1]<=descriptionPanel->GetTall()&&richClip[3]>richClip[1],"actual long text clip remains bounded by the viewport");
 int bx,by,bw,bh;descriptionPanel->GetBounds(bx,by,bw,bh);
 descriptionPanel->Prepare({bx,by,bw,bh},originalDescription);gui.RunFrame();
 Check(!actualBar->IsVisible()&&actualText->GetTall()<bh,"actual short resource description hides native scrollbar");
 descriptionPanel->Prepare({bx,by,bw,bh},infoItems.at(textItem).description);gui.RunFrame();nativeSurface.SolveTraverse(root,true);
 auto* wheel=new KeyValues("MouseWheeled","delta",-1);descriptionPanel->OnMessage(wheel,0);wheel->deleteThis();gui.RunFrame();
 const int step=int(float(descriptionPanel->GetTall())*.1f);
 Check(actualBar->GetValue()==step&&host.nodes[canvas].y==-step,"actual wheel callback and queued slider message move canvas");
 auto* arrow=dynamic_cast<WeaponInfoArrow*>(actualBar->GetButton(1));
 Check(arrow!=nullptr,"native scrollbar owns actual original-style arrow adapter");
 Check(arrow->GetFont()==closeFont,"actual down arrow keeps selected font");
 Check(actualText->GetFgColor()==ScrollColor("richTextFg")&&actualText->GetBgColor()==ScrollColor("richTextBg")&&actualText->GetBorder()==nullptr,"actual RichText uses original resource colors and no fallback border");
 Check(actualBar->GetBgColor()==ScrollColor("barBg")&&actualBar->GetSlider()->GetFgColor()==ScrollColor("sliderFg"),"actual bar and slider apply original resource colors");
 const auto priorFocus=nativeInput.focus;arrow->SetArmed(true);arrow->OnMousePressed(vgui::MOUSE_LEFT);
 Check(inputCapture.captured==arrow->GetVPanel()&&nativeInput.focus==priorFocus,"actual arrow captures mouse without requesting focus");
 arrow->OnMouseFocusTicked();gui.RunFrame();Check(actualBar->GetValue()==2*step&&host.nodes[canvas].y==-2*step,"native arrow tick scrolls through actual SDK bar action");
 arrow->OnMouseReleased(vgui::MOUSE_LEFT);Check(inputCapture.captured==0&&!arrow->IsSelected(),"native arrow release clears capture and selection");
 arrow->SetEnabled(false);const auto captureBefore=inputCapture.captureCalls;arrow->OnMousePressed(vgui::MOUSE_LEFT);
 Check(inputCapture.captureCalls==captureBefore&&!arrow->IsSelected(),"disabled native arrow rejects capture");arrow->SetEnabled(true);
 arrow->OnMousePressed(vgui::MOUSE_RIGHT);Check(inputCapture.captureCalls==captureBefore,"native arrow ignores unconfigured mouse button");
 int sx,sy;actualBar->GetSlider()->GetPos(sx,sy);
 Check(sx==2&&sy==12&&actualBar->GetSlider()->GetWide()==12,"actual slider uses original x2 and square arrow layout");
 int first,last;actualBar->GetSlider()->GetNobPos(first,last);capture.images.clear();capture.images.reserve(64);capture.recordImages=true;
 host.PaintTraverse(actualBar->GetSlider()->GetVPanel(),true);capture.recordImages=false;
 Check(capture.images.size()==(last-first>24?3:2),"actual original-style thumb emits expected cap/body images");
 if(capture.images.size()>=2){
   const auto& topImage=capture.images.front();const auto& bottomImage=capture.images.back();
   Check(topImage.y0==first&&bottomImage.y0==last-12&&topImage.x1==actualBar->GetSlider()->GetWide()-1,"actual short thumb retains original bottom-end12 crop and one-pixel right margin");
 }
 const auto slider=actualBar->GetSlider()->GetVPanel();int ax,ay;host.GetAbsPos(slider,ax,ay);
 nativeInput.SetCursorPos(ax+1,ay+(first+last)/2);nativeInput.left=true;
 auto* press=new KeyValues("MousePressed","code",int(vgui::MOUSE_LEFT));host.Client(slider)->OnMessage(press,0);press->deleteThis();
 Check(nativeInput.capture==slider,"actual SDK slider press captures native input");
 auto* move=new KeyValues("OnCursorMoved","x",1,"y",actualBar->GetSlider()->GetTall()+100);host.Client(slider)->OnMessage(move,0);move->deleteThis();gui.RunFrame();
 int minimum,maximum;actualBar->GetRange(minimum,maximum);
 Check(actualBar->GetValue()==maximum-actualBar->GetRangeWindow()&&host.nodes[canvas].y==-actualBar->GetValue(),"actual SDK thumb drag clamps at bottom and updates canvas");
 auto* release=new KeyValues("MouseReleased","code",int(vgui::MOUSE_LEFT));host.Client(slider)->OnMessage(release,0);release->deleteThis();nativeInput.left=false;
 Check(nativeInput.capture==0,"actual SDK slider release frees native capture");
 hover=-1;detailItem=tableItem;UpdateDetail();Check(!descriptionPanel->IsVisible(),"table mode hides description scroller");
 detailItem=textItem;UpdateDetail();const auto kept=detailItem;hover=-1;UpdateDetail();Check(detailItem==kept&&descriptionPanel->IsVisible(),"hover exit retains the last valid description");
 const auto readBefore=reads,uploadBefore=uploads;
 nativeSurface.SolveTraverse(root,true);menuLayer->PaintOverlay();gui.RunFrame();
 allocation_probe::calls=0;allocation_probe::active=true;
 for(int i=0;i<50;++i){UpdateDetail();menuLayer->PaintOverlay();}
 allocation_probe::active=false;
 std::cout<<"native detail paint50 allocations="<<allocation_probe::calls<<" reads="<<reads-readBefore<<" uploads="<<uploads-uploadBefore<<'\n';
 Check(reads==readBefore&&uploads==uploadBefore,"stable actual native detail reads and uploads stay zero");
 Check(allocation_probe::calls==0,"stable actual native detail performs no global C++ new");
 inputCapture.x=inputCapture.y=-100;BuyMenuFrame();Paint();gui.RunFrame();
 allocation_probe::calls=0;allocation_probe::active=true;
 for(int i=0;i<50;++i){BuyMenuFrame();Paint();}
 allocation_probe::active=false;
 std::cout<<"native full frame+paint50 allocations="<<allocation_probe::calls<<" reads="<<reads-readBefore<<" uploads="<<uploads-uploadBefore<<'\n';
 Check(allocation_probe::calls==0&&reads==readBefore&&uploads==uploadBefore,"stable full actual controller and native paint allocate/read/upload nothing");
 const int oldWidth=descriptionPanel->GetWide();capture.w=1920;capture.h=1080;Layout();UpdateDetail();gui.RunFrame();
 Check(descriptionPanel->GetWide()>oldWidth&&detailLayoutW==1920&&detailLayoutH==1080,"resolution change reflows actual description controls");
 font=15;UpdateDetail();gui.RunFrame();Check(detailLayoutFont==15,"font change invalidates actual description layout");
 Close(false);Check(!menuLayer->IsVisible(),"close hides actual native control root");
 menuLayer.reset();detailPanel=nullptr;descriptionPanel=nullptr;gui.RunFrame();Check(host.nodes.size()==1,"actual SDK hierarchy retires all descendants");
 capture.w=1280;capture.h=720;
 infoItems.at(textItem).description=originalDescription;
}
void SearchFocus(){
 using namespace buy_native;
 Host host;Gui gui(host);Keys keys;buy_native::Input nativeInput;Schemes nativeSchemes;Localize localize;vgui::SystemBoundary system;
 buy_native::Surface nativeSurface(host,gui,capture.w,capture.h);
 g_pVGui=&gui;g_pVGuiPanel=&host;g_pKeyValuesSystem=&keys;g_pVGuiInput2=&nativeInput;g_pVGuiSurface2=&nativeSurface;
 g_pVGuiSchemeManager2=&nativeSchemes;g_pVGuiSystem=&system;g_pVGuiLocalize=&localize;
 std::unique_ptr<vgui::EditablePanel> embedded;
 struct Cleanup {std::unique_ptr<vgui::EditablePanel>& embedded;~Cleanup(){
   menuLayer.reset();detailPanel=nullptr;descriptionPanel=nullptr;searchEntry=nullptr;capture.embedded=1;
   embedded.reset();
   g_pVGui=nullptr;g_pVGuiPanel=nullptr;g_pKeyValuesSystem=nullptr;g_pVGuiInput2=nullptr;g_pVGuiSurface2=nullptr;
   g_pVGuiSchemeManager2=nullptr;g_pVGuiSystem=nullptr;g_pVGuiLocalize=nullptr;
 }} cleanup{embedded};
 // Controlled root boundary, not a claim about the actual hw embedded root.
 // Both ancestors and the TextEntry execute linked SDK navigation/message code.
 embedded=std::make_unique<vgui::EditablePanel>(nullptr,"SearchFixtureRoot");
 embedded->SetBounds(0,0,capture.w,capture.h);embedded->SetVisible(true);
 capture.embedded=nativeSurface.embedded=embedded->GetVPanel();
 nativeInput.keyHost=&host;nativeInput.keyRoot=embedded->GetVPanel();
 Fresh();visible=false;inputCapture.left=inputCapture.right=false;inputCapture.x=inputCapture.y=-100;
 OpenMenu(false);gui.RunFrame();Offers();
 Check(visible&&searchEntry&&menuLayer,"production open constructs actual TextEntry under menu layer");
 Check(searchEntry&&searchEntry->GetVParent()==menuLayer->GetVPanel(),"actual search entry retains production native parent");
 if(!searchEntry||!menuLayer){menuLayer.reset();embedded.reset();return;}
 auto message=[&](const char* name,const char* field,int value){
   const auto target=nativeInput.GetFocus();
   if(!target)return false;
   auto* data=new KeyValues(name,field,value);host.SendMessage(target,data,0);data->deleteThis();gui.RunFrame();return true;
 };
 Click(40);gui.RunFrame();
 Check(nativeInput.GetFocus()==searchEntry->GetVPanel()&&searchEntry->HasFocus(),"real RequestFocus traverses SDK navigation to actual search entry");
 const auto commandsBefore=commands.size();
 int down=1,key='r';const char* binding="rebuy";int consumed=0;VGUI2Extension_CallbackContext context{};context.pPluginReturnValue=&consumed;
 overlay.Key_Event(down,key,binding,&context);
 Check(visible&&commands.size()==commandsBefore&&context.Result!=VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS,"focused search lets native text path receive R without buying");
 Check(message("KeyTyped","unichar",'b')&&message("KeyTyped","unichar",'r')&&message("KeyTyped","unichar",0x6b66),"dispatch text only through real SDK-selected key focus");
 wchar_t text[49]{};searchEntry->GetText(text,sizeof(text));
 Check(std::wstring_view(text)==L"br\u6b66","actual TextEntry edits ASCII and Unicode via SDK KeyTyped map");
 Check(message("KeyCodeTyped","code",int(vgui::KEY_BACKSPACE)),"SDK key focus receives backspace edit");
 searchEntry->GetText(text,sizeof(text));Check(std::wstring_view(text)==L"br","actual TextEntry backspace edits its buffer");
 BuyMenuFrame();Check(query==L"br","production frame consumes actual SDK-edited search text");
 Click(team==1?21:20);gui.RunFrame();
 Check(!searchEntry->IsKeyBoardInputEnabled(),"clicking team tab disables native search keyboard input");
 Check(!searchEntry->HasFocus()&&nativeInput.GetFocus()!=searchEntry->GetVPanel(),"clicking team tab retires SDK search focus through its actual parent");
 const auto afterTab=commands.size();context={};context.pPluginReturnValue=&consumed;down=1;key='r';binding="rebuy";
 overlay.Key_Event(down,key,binding,&context);
 Check(!visible&&commands.size()==afterTab+1&&commands.back()=="rebuy_in\n","R resumes menu rebuy after leaving search focus");
 Check(context.Result==VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS,"menu consumes resumed shortcut instead of leaking native binding");
 for(const int shortcut:{'b','1'}){
   Close(false);OpenMenu(false);gui.RunFrame();Offers();Click(40);gui.RunFrame();
   Check(searchEntry->HasFocus(),"reopened actual search receives SDK focus again");
   const auto beforeShortcut=commands.size();context={};context.pPluginReturnValue=&consumed;down=1;key=shortcut;binding=shortcut=='b'?"buy":"slot1";
   overlay.Key_Event(down,key,binding,&context);
   Check(visible&&category==-1&&commands.size()==beforeShortcut&&context.Result!=VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS,"B and number bindings remain text while actual search is focused");
   Click(team==1?21:20);gui.RunFrame();
   Check(!searchEntry->HasFocus()&&!searchEntry->IsKeyBoardInputEnabled(),"reopened search leaves SDK focus after tab click");
   context={};context.pPluginReturnValue=&consumed;overlay.Key_Event(down,key,binding,&context);
   Check(shortcut=='b'?!visible:(visible&&category==1),"B closes and number opens category after leaving actual search focus");
   Check(context.Result==VGUI2Extension_Result::SUPERCEDE_SKIP_PLUGINS,"resumed B and number shortcuts stay inside menu input owner");
 }
 Close(false);
 Check(!menuLayer->IsVisible()&&!searchEntry->IsVisible()&&!searchEntry->IsKeyBoardInputEnabled(),"close hides actual search and retires keyboard input");
 menuLayer.reset();detailPanel=nullptr;descriptionPanel=nullptr;searchEntry=nullptr;gui.RunFrame();embedded.reset();gui.RunFrame();
 Check(host.nodes.size()<=1,"search focus hierarchy releases all actual SDK descendants");
 std::cout<<"search focus boundary=actual SDK EditablePanel root and TextEntry; controlled input delivery; no live hw root or IME claim\n";
}
}
namespace csol::client {
metahook_api_t* api{};cl_enginefunc_t engine{};cl_exportfuncs_t downstream{};ref_params_t view{};
std::vector<std::byte> ReadResource(const char* path){++fixture::reads;std::ifstream in(fixture::resources/path,std::ios::binary);if(!in)throw std::runtime_error(std::string("missing fixture resource ")+path);std::vector<char> raw((std::istreambuf_iterator<char>(in)),{});std::vector<std::byte> bytes(raw.size());std::memcpy(bytes.data(),raw.data(),raw.size());return bytes;}
void SynchronizeSession(){++fixture::syncs;}void ClearNativeWeaponSelection(){}
const goldcraft::csol_resources::Api& ResourceRefreshApi(){static goldcraft::csol_resources::Api value{};return value;}
bool ResourceFontIsTracked(void** owner){return owner==&privateFont;}
void* RegisterResourceFont(void*&,const char*,const char*,std::span<const std::byte>,unsigned long*){return nullptr;}
}
int main(int argc,char** argv){
 if(argc!=2)return 2;fixture::resources=argv[1];fixture::Setup();
 try{fixture::Check(csol::client::Load(),"complete actual client loads real buy resource and TGA codec");fixture::Check(csol::client::presentation.normalizedLayout,"normalized schema parsed");fixture::StateTests();fixture::RebuyTests();fixture::RefreshTests();fixture::HotPath();fixture::NativeControls();fixture::SearchFocus();}
 catch(const std::exception& e){++fixture::failures;std::cerr<<"EXCEPTION "<<e.what()<<'\n';}
 csol::client::extension=nullptr;csol::client::ResetBuyMenu();std::cout<<"checks="<<fixture::checks<<" failures="<<fixture::failures<<'\n';return fixture::failures?1:0;
}
