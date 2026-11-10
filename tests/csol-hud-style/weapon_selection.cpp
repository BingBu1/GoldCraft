// SPDX-License-Identifier: GPL-3.0-or-later
// Complete production HUD consumer; resources/surface and pending inventory
// are controlled boundaries. Coordinates come from the two original consumers.
#define main UnusedCompleteHudRegression
#include "client.cpp"
#undef main
int main()
{
    using namespace csol::client;using namespace fixture;using csol::hud_style::Art;
    Configure();SetupResources();Seed();Prepare();PrepareSelection();
    auto add=[](int id,int slot,int position,const char* name,const char* title)
    {
        csol::WeaponDefinition d;d.id=id;d.slot=slot;d.position=position;d.name=name;
        selectionDefinitions.emplace(id,d);
        auto& item=catalog.weapons[name];item.title=title;item.weapon=std::string("resource/csol/hud/v1/")+name+".tga";
        item.selected=std::string("resource/csol/hud/v1/")+name+"_s.tga";
        item.selectedKnown=true;
        files[item.weapon]=Tga(170,45);files[item.selected]=Tga(170,45);
    };
    add(16,1,0,"weapon_usp","USP45");add(29,2,0,"weapon_knife","Knife");
    auto prepare=[](){cardsRetryAfter=0;PrepareSelection();};
    selectionItems={400,16,29};selectionPending=29;prepare();Draw();
    Check(selectionHudOwner&&selectionRows.size()==4,"pending selection retains the equipped off-row plus three inventory rows");
    Check(selectionRows[0].id==400&&selectionRows[0].equipped&&!selectionRows[0].selected&&
        selectionRows[1].id==29&&selectionRows[1].expanded&&selectionRows[1].selected&&
        selectionRows[2].id==16&&!selectionRows[2].expanded&&selectionRows[3].id==400&&!selectionRows[3].expanded,
        "original bottom-up slot order expands the pending slot and keeps other heads compact");
    auto quad=[](Art a,int y,int bottom,int alpha,bool additive=false)
    {return std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& q)
        {return q.texture==art[unsigned(a)].id&&q.x==1106&&q.right==1276&&q.y==y&&q.bottom==bottom&&q.color[3]==alpha&&q.additive==additive;});};
    Check(quad(Art::WeaponCard,571,632,225),"server deployed identity remains the bottom off-row");
    Check(quad(Art::WeaponSelectedCard,506,567,100),"selected on TGA uses alpha100 stretched to original61px row");
    Check(quad(Art::WeaponSelectionFrame,506,567,255,true),"original 170x61 additive selection frame is a separate layer");
    Check(quad(Art::WeaponListRow,480,502,225)&&quad(Art::WeaponListRow,454,476,225),"compact original22px rows retain4px gaps");
    Check(std::any_of(capture.texts.begin(),capture.texts.end(),[](const auto& t){return t.value==L"3 Knife"&&t.x==1114&&t.y==510;}),"expanded title offset is8,4");
    Check(std::any_of(capture.texts.begin(),capture.texts.end(),[](const auto& t){return t.value==L"2 USP45"&&t.x==1114&&t.y==486;}),"compact title offset is8,6");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q){return q.texture==weaponImage.id&&q.y==586;}),"pending list preserves deployed card and its current ammunition");
    Check(WeaponDigits()=="130","pending knife selection does not replace equipped firearm ammunition");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==selectionCards.at(29).selectedImage.id&&q.y==521;}),"selected branch consumes weapon_s at original15px inset");
    const auto stableReads=reads,stableTextures=capture.live.size();Draw();
    Check(reads==stableReads&&capture.live.size()==stableTextures,"stable redraw neither reads nor uploads resources");
    auto auditDraw=[](int active,int pending,const char* label)
    {
        state.id=active;state.kind=active==400?csol::hud_style::AmmoKind::Magazine:csol::hud_style::AmmoKind::None;
        selectionPending=pending;preparedSelection=0;
        const auto readBefore=reads;
        const auto uploadBefore=capture.next;
        capture.record=false;allocation_probe::calls=0;allocation_probe::active=true;
        BeginHudStyle(0,0);DrawHudStyle();Paint();
        allocation_probe::active=false;capture.record=true;
        Check(!allocation_probe::calls&&reads==readBefore&&capture.next==uploadBefore,label);
    };
    auditDraw(16,16,"late stock CurWeapon after frame uses warmed resources with zero allocation/read/upload in Begin and Paint");
    Check(HudStyleWeaponVisible()&&selectionHudOwner,"late stock switch retains modern HUD ownership without one-frame yellow fallback");
    auditDraw(400,-1,"empty pending sentinel builds compact rows without draw allocation/read/upload");
    selectionPending=29;Draw();
    add(725,0,8,"weapon_extra","Extra");selectionItems={400,725,16,29};selectionPending=725;prepare();Draw();
    Check(selectionRows.size()==5&&selectionRows[0].equipped&&selectionRows[1].id==29&&!selectionRows[1].expanded&&
        selectionRows[2].id==16&&!selectionRows[2].expanded&&selectionRows[3].id==725&&selectionRows[3].selected&&
        selectionRows[4].id==400&&selectionRows[4].expanded&&!selectionRows[4].selected,
        "high IDs use validated definition positions; every item in selected slot expands");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==art[unsigned(Art::WeaponCard)].id&&q.bottom-q.y==61&&q.color[3]==225;}),"other expanded item uses off TGA at alpha225");
    catalog.weapons.at("weapon_cannon").selected="resource/csol/hud/v1/weapon_cannon_s.tga";
    catalog.weapons.at("weapon_cannon").selectedKnown=true;
    files[catalog.weapons.at("weapon_cannon").selected]=Tga(170,45);
    selectionPending=400;prepare();Draw();
    Check(selectionHudOwner&&selectionCards.at(400).selectedImage.id&&selectionCards.at(725).image.id,
        "frame preparation warms independent normal and weapon_s images before selection ownership");
    Check(selectionCards.at(400).selectedImage.id!=selectionCards.at(400).image.id,
        "normal and selected sprites remain separate cached resources");
    definition.ammo1=1;definition.maxAmmo1=200;definition.ammo2=2;definition.maxAmmo2=10;
    selectionReserve[1]=50;selectionReserve[2]=0;preparedSelection=0;Draw();
    const auto currentRow=std::find_if(selectionRows.begin(),selectionRows.end(),[](const auto& row){return row.id==400;});
    const int cardTop=screenHeight-88-61-4-22-4-22-4-61-4-61;
    auto fill=[](int x,int y,int width,int height,std::array<int,4> color)
    {return std::any_of(capture.fills.begin(),capture.fills.end(),[&](const auto& f)
        {return f.x==x&&f.y==y&&f.right==x+width&&f.bottom==y+height&&f.color==color;});};
    Check(currentRow!=selectionRows.end()&&fill(1116,cardTop+18,5,4,{0,160,0,255})&&
        fill(1121,cardTop+18,15,4,{255,255,255,128})&&fill(1141,cardTop+18,20,4,{255,255,255,128}),
        "expanded ammo bars use primary reserve/max, green fill and white residual at original20x4 geometry; second follows25px");
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==selectionCards.at(400).selectedImage.id&&q.color==std::array<int,4>{192,192,192,255};}),
        "available selected sprite uses ScaleColors white192 tint");
    selectionEmpty.insert(400);selectionReserve[1]=0;Draw();
    Check(std::any_of(capture.quads.begin(),capture.quads.end(),[](const auto& q)
        {return q.texture==selectionCards.at(400).selectedImage.id&&q.color==std::array<int,4>{128,8,8,255};}),
        "unavailable selected sprite uses original red128,8,8 tint");
    Check(!fill(1116,cardTop+18,20,4,{255,255,255,128}),"zero primary reserve suppresses both bars");
    selectionEmpty.clear();selectionReserve[1]=1;selectionReserve[2]=20;Draw();
    Check(fill(1116,cardTop+18,1,4,{0,160,0,255})&&fill(1141,cardTop+18,20,4,{0,160,0,255}),
        "small positive reserve retains minimum1px and overfull secondary clamps to1");
    selectionPending=29;Delete(selectionCards.at(29).selectedImage);
    catalog.weapons.at("weapon_knife").selected.clear();preparedSelection=0;prepare();Draw();
    Check(selectionHudOwner&&quad(Art::WeaponSelectionFrame,506,567,255,true),
        "explicit absent original weapon_s keeps the original frame without inventing a normal silhouette");
    catalog.weapons.at("weapon_knife").selectedKnown=false;preparedSelection=0;prepare();Draw();
    Check(!selectionHudOwner,"legacy missing selected schema cannot claim original no-sprite evidence");
    catalog.weapons.at("weapon_knife").selectedKnown=true;prepare();
    selectionPending=-1;Draw();
    Check(selectionHudOwner&&selectionReady&&selectionRows.size()==4&&
        std::none_of(selectionRows.begin()+1,selectionRows.end(),[](const auto& row){return row.expanded||row.selected;}),
        "original empty pending sentinel keeps equipped row and compact inventory without selected frame");
    selectionPending=0;Draw();Check(!selectionHudOwner&&selectionReady&&selectionRows.size()==1&&selectionRows[0].equipped,
        "confirmation/cancel/timeout removes pending rows and preserves the deployed off-row");
    selectionPending=725;Draw();Check(selectionHudOwner,"reopening selection uses retained immutable textures");
    BeginHudStyle(1,0);Check(!selectionHudOwner,"server hide releases selection ownership");
    BeginHudStyle(0,1);Check(!selectionHudOwner,"intermission releases selection ownership");
    view.health=0;BeginHudStyle(0,0);Check(!selectionHudOwner,"death releases selection ownership");view.health=100;
    Draw();ConstrainHudStyle(true,false,true);Check(!selectionHudOwner,"native ownership constraint preserves fallback menu");
    Delete(selectionCards.at(725).selectedImage);files.erase(catalog.weapons.at("weapon_extra").selected);selectionCards.at(725).selectedRetryAfter=0;preparedSelection=0;prepare();Draw();
    Check(!selectionHudOwner&&!selectionReady&&HudStyleWeaponVisible(),"missing selected silhouette keeps native selection and current ammo usable");
    ResetHudStyle();Check(!selectionHudOwner&&selectionRows.empty(),"session reset closes pending visual rows");
    HudStyleVideoInit();Check(selectionCards.empty()&&capture.live.empty(),"video/resource retirement releases every cached list texture");
    privateFont=nullptr;extension=nullptr;surface=nullptr;schemes=nullptr;
    std::cout<<"weapon_selection checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
