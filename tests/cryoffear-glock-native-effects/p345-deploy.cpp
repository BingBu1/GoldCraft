// SPDX-License-Identifier: GPL-3.0-or-later
#define main P345CompositionMain
#include "../cryoffear-p345-weapon/main.cpp"
#undef main
BOOL CCSPlayerWeapon::DefaultDeploy(char*,char*,int,char*,int){std::abort();}
int CCSPlayerWeapon::DefaultReload(int,int,float){std::abort();}
bool CCSPlayerWeapon::DefaultShotgunReload(int,int,float,float,const char*,const char*){std::abort();}
void CCSPlayerWeapon::KickBack(float,float,float,float,float,float,int){std::abort();}
void CCSPlayerWeapon::SendWeaponAnim(int,int){std::abort();}
#include "sdk-on-create.inc"
int main(int argc,char** argv)
{
    if(argc!=4)return 1;std::ifstream file(argv[1],std::ios::binary);if(!file)return 1;
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
    unsigned rows{};
    for(unsigned armor:{0xc0000000u,0u,0x3f000000u,0x3f800000u,0x40000000u,0x7fc00001u})for(int clip:{0,8})
    {
        Initialize();weapon.pev->armorvalue=Float(armor);weapon.m_iClip=clip;
        player.m_rgAmmo[1]=24;deployOrder.clear();deployRandom.clear();traceDeploy=true;
        Check(weapon.Deploy());traceDeploy=false;
        const bool suppressed=armor==0x3f800000;
        const bool heard=std::find(deployOrder.begin(),deployOrder.end(),unsigned(P345Effect::Sleeve))!=deployOrder.end();
        Check(heard!=suppressed&&Bits(weapon.pev->armorvalue)==(suppressed?0u:armor));
        const std::vector<std::pair<int,int>> expectedRandom=suppressed?
            std::vector<std::pair<int,int>>{{-5,5}}:std::vector<std::pair<int,int>>{{-5,5},{0,2}};
        Check(deployRandom==expectedRandom&&weapon.ActionState().clip==clip);++rows;
    }
    std::cout<<rows<<" actual P345 deploy armor cases; "<<checks<<" SDK checks; 0 failures\n";
}
