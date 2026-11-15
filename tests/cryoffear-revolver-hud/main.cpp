// SPDX-License-Identifier: GPL-3.0-or-later
#define main OriginalHudRegression
#include "tests/csol-hud-style/client.cpp"
#undef main
#include "native/csol/client/weapons/weapon_module.hpp"
#include "native/guest/cof/client/revolver_hud.hpp"
#include <limits>

// Rendering is independently verified. This boundary checks that the real
// shared module registry invokes its crosshair consumer once, not per pistol.
namespace csol::client
{
unsigned crosshairCalls{};
int crosshairHidden{},crosshairIntermission{};
bool crosshairAmmo{};
void InitializeGuestCofCrosshair() { std::abort(); }
void ResetGuestCofCrosshair() noexcept { std::abort(); }
void GuestCofCrosshairVideoInit() noexcept { std::abort(); }
void ShutdownGuestCofCrosshair() noexcept { std::abort(); }
void DrawGuestCofCrosshair(int hidden,int intermission,bool ammo) noexcept
{++crosshairCalls;crosshairHidden=hidden;crosshairIntermission=intermission;crosshairAmmo=ammo;}
}

int main(int argc,char** argv)
{
    using namespace csol::client;using namespace fixture;
    char executable[]="revolver-hud";char* arguments[]{executable,nullptr};
    if(OriginalHudRegression(1,arguments))return 1;
    Configure();SetupResources();Seed();
    constexpr const char* prefix="resource/csol/hud/v1/revolver";
    const auto* module=FindWeaponModule(898);
    Check(module&&module->hudState&&module->ammoKind==csol::hud_style::AmmoKind::Magazine,
        "real statically registered leftwheel module supplies typed frame producer");
    Check(FindWeaponModule(896)&&FindWeaponModule(897),"previous guest modules remain registered");
    for(int id:{899,900,901})
    {
        const auto* guest=FindWeaponModule(id);
        Check(guest&&guest->ammoKind==csol::hud_style::AmmoKind::Magazine&&!guest->dynamicCrosshair&&
            !guest->drawHud&&guest->ammoRect.right==32&&guest->ammoRect.bottom==32,
            "automatic/VP70 static magazine modules use their own typed original icon and single shared crosshair");
    }
    crosshairCalls=0;
    for(const auto* item:WeaponModules())if(item->drawHud)item->drawHud(13,1,false);
    Check(crosshairCalls==1&&crosshairHidden==13&&crosshairIntermission==1&&!crosshairAmmo,
        "one shared guest crosshair callback preserves actual dispatcher arguments without duplicate blending");
    const std::string base=prefix;
    std::string frames;
    for(int i=0;i<6;++i)
    {
        if(i)frames+=',';
        frames+='"'+base+std::to_string(i)+".tga\"";
        files[base+std::to_string(i)+".tga"]=Tga(32,32);
    }
    const std::string row="{\"name\":\"weapon_cof_revolver\",\"title\":\"客串武器·Taurus .357\","
        "\"weapon\":\""+std::string(model)+"\",\"selected\":\""+model+"\","
        "\"ammo\":\""+base+"0.tga\",\"ammo2\":\"\",\"ammoFrames\":["+frames+"]}";
    Put(csol::hud_style::CatalogPath,"{\"version\":1,\"weapons\":["+row+"],\"portraits\":[]}");
    definition.id=898;definition.name="weapon_cof_revolver";definition.slot=1;
    state={};state.id=898;state.slot=1;state.kind=csol::hud_style::AmmoKind::Magazine;
    state.reserve=500;Prepare();PrepareSelection();
    Check(HudStyleWeaponResourcesReady(898,6),"actual cold HUD loads all six frames before readiness");
    const auto stableReads=reads;
    const auto handles=selectionCards.at(898).ammoFrames;
    for(int clip:{0,1,2,3,4,5,6,75,511,65535,std::numeric_limits<int>::max()})
    {
        state.clip=clip;module->hudState(state);Draw();
        const auto index=guest::cof::client::RevolverAmmoFrame(clip);
        Check(state.ammoFrame==index&&ammoImage.id==handles[index].id,"clip selects proved original frame with explicit high-ammo projection");
        Check(WeaponDigits()==std::to_string(clip)+"500","authoritative clip and reserve remain complete numbers");
        Check(std::any_of(capture.quads.begin(),capture.quads.end(),[&](const auto& q)
            {return q.texture==handles[index].id&&q.additive&&q.right-q.x==32&&q.bottom-q.y==32;}),
            "selected frame uses existing additive32px HUD icon placement");
    }
    Check(reads==stableReads,"frame selection performs no resource reads");
    capture.record=false;allocation_probe::calls=0;allocation_probe::active=true;
    for(int i=0;i<100;++i)
    {state.clip=i%6;module->hudState(state);BeginHudStyle(0,0);DrawHudStyle();Paint();}
    allocation_probe::active=false;capture.record=true;
    Check(allocation_probe::calls==0&&reads==stableReads,"100 actual hot redraws allocate and read zero resources");
    state.ammoFrame=6;Draw();Check(!HudStyleWeaponVisible(),"invalid explicit frame never borrows a static or other frame");
    for(int missing=0;missing<6;++missing)
    {
        const auto path=base+std::to_string(missing)+".tga";
        auto saved=files.at(path);InvalidateSelectedRow();const auto live=capture.live.size();files.erase(path);
        state.clip=missing;module->hudState(state);PrepareSelection();Draw();
        Check(!HudStyleWeaponResourcesReady(898,6)&&!HudStyleWeaponVisible()&&capture.live.size()==live,
            "missing independent frame rejects entire staged row without leaking textures");
        const auto failedReads=reads;PrepareSelection();Check(reads==failedReads,"missing frame uses bounded existing cold retry");
        files[path]=Tga(31,32);selectionCards.at(898).imageRetryAfter=0;PrepareSelection();
        Check(!HudStyleWeaponResourcesReady(898,6)&&capture.live.size()==live,"wrong frame size rejects atomic row");
        files[path]=std::move(saved);selectionCards.at(898).imageRetryAfter=0;PrepareSelection();Draw();
        Check(HudStyleWeaponResourcesReady(898,6)&&HudStyleWeaponVisible(),"verified frame recovery prepares full row");
    }
    HudStyleVideoInit();Check(capture.live.empty(),"video replacement deletes all frame textures exactly once");
    if(argc==2)
    {
        SetupResources();const std::filesystem::path root=argv[1];
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.is_regular_file())
        {
            std::ifstream input(entry.path(),std::ios::binary);
            const std::vector<char> data{std::istreambuf_iterator<char>(input),{}};
            Put(entry.path().lexically_relative(root).generic_string(),std::string(data.begin(),data.end()));
        }
        bool staticMagazine{};
        for(const auto& [id,key]:{std::pair{900,"tmp"},std::pair{901,"mp5"}})
        {
            if(!std::filesystem::exists(root/(std::string("resource/csol/hud/v1/cof_")+key+"_ammo.tga")))continue;
            staticMagazine=true;
            definition.id=id;definition.name=std::string("weapon_cof_")+key;definition.slot=0;
            state={};state.id=id;state.slot=0;state.kind=csol::hud_style::AmmoKind::Magazine;state.reserve=500;state.clip=30;
            retryAfter=0;Prepare();PrepareSelection();
            Check(loaded&&HudStyleWeaponResourcesReady(id,1),"original automatic magazine prepares through real catalog and texture owner");
            if(!loaded||!HudStyleWeaponResourcesReady(id,1))return 1;
            const auto& actual=selectionCards.at(id);const auto& source=catalog.weapons.at(definition.name);
            const auto expected=csol::DecodeTGA(files.at(source.ammo));const auto& upload=capture.uploads.at(actual.ammo.id);
            Check(expected.RGBA==upload.RGBA&&upload.Width==32&&upload.Height==32,"original automatic magazine pixels reach owned texture unchanged");
            const auto tmpReads=reads;
            for(int clip:{0,1,30,75,511,65535,std::numeric_limits<int>::max()})
            {
                state.clip=clip;Draw();
                Check(ammoImage.id==actual.ammo.id&&WeaponDigits()==std::to_string(clip)+"500","automatic magazine retains complete authoritative clip/reserve and its own icon");
            }
            Check(reads==tmpReads,"automatic magazine numeric updates do not open or upload resources");
            capture.record=false;allocation_probe::calls=0;allocation_probe::active=true;
            for(int i=0;i<100;++i){state.clip=i;BeginHudStyle(0,0);DrawHudStyle();Paint();}
            allocation_probe::active=false;capture.record=true;
            Check(allocation_probe::calls==0&&reads==tmpReads,"100 automatic magazine hot redraws allocate and read zero resources");
            HudStyleVideoInit();Check(capture.live.empty(),"automatic magazine textures retire on video replacement");
        }
        if(!staticMagazine)
        {
        retryAfter=0;Prepare();PrepareSelection();
        Check(loaded&&HudStyleWeaponResourcesReady(898,6),"actual thirteen-file author output prepares real catalog and full row");
        const auto& actualFrames=selectionCards.at(898).ammoFrames;
        const auto sourceRow=catalog.weapons.at(definition.name);
        for(int i=0;i<6;++i)
        {
            const auto expected=csol::DecodeTGA(files.at(sourceRow.ammoFrames[i]));
            const auto& uploaded=capture.uploads.at(actualFrames[i].id);
            Check(expected.RGBA==uploaded.RGBA&&expected.Width==32&&expected.Height==32,
                "real original-converted frame pixels reach owned runtime texture unchanged");
            state.clip=i;module->hudState(state);Draw();
            Check(ammoImage.id==actualFrames[i].id&&WeaponDigits()==std::to_string(i)+"500",
                "real exported resource frame and numeric ammunition agree");
        }
        HudStyleVideoInit();Check(capture.live.empty(),"real converted frame ownership retires on video replacement");
        }
    }
    extension=nullptr;privateFont=nullptr;surface=nullptr;schemes=nullptr;
    std::cout<<"Revolver actual HUD checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
