// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace upgrade_test
{
void ResourceCases()
{
    const auto tables=RealTables();const auto& levels=tables->levels;
    Check(levels.rows.size()==101,"real table retains physical levels 0 through 100");
    Check(levels.rows[0].experience==0&&levels.rows[1].experience==2000&&levels.rows[30].experience==3275&&
        levels.rows[31].experience==3330&&levels.rows[100].experience==7000,"fixed original XP resource anchors");
    Check(levels.rows[30].values==std::array<float,5>{1200,5.8f,50,300,800},"all five physical stat columns mapped in original order");
    for(std::size_t row=31;row<=100;++row)
        Check(levels.rows[row].values==std::array<float,5>{},"original blank high-level attribute cells convert to zero");
    Check(levels.ExperienceLevel(101)==&levels.rows[100]&&levels.ExperienceLevel(INT32_MAX)==&levels.rows[100],"XP alone clamps above level100");
    float value=123;Check(!levels.StatValue(UpgradeStat::Health,101,value)&&value==123,"stat lookup does not inherit XP clamp");
    Check(!levels.ExperienceLevel(-1)&&!levels.StatValue(UpgradeStat::Health,-1,value),"negative table index is rejected");
    const auto* standard=tables->classes.Find(0);const auto* repeated=tables->classes.Find(45);
    Require(standard&&repeated,"default and original class45 present");
    Check(standard->increases==std::array<int,5>{20,20,20,20,20},"inserted original class0 cap increments");
    Check(repeated->increases==std::array<int,5>{18,23,20,19,20},"real class45 cap increments");
    Check(repeated->order==std::vector<UpgradeStat>{UpgradeStat::Health,UpgradeStat::Damage,UpgradeStat::Armor,
        UpgradeStat::Health,UpgradeStat::Damage,UpgradeStat::Armor,UpgradeStat::Ammo,UpgradeStat::Ammo,UpgradeStat::Speed},
        "class45 hdahdamms retains repeated letters and physical order");
    UpgradeClasses replaced;ConfigError detail;
    Require(ReadUpgradeClasses("id,h,d,s,a,m,order\r\n0,1,2,3,4,5,sdh\r\n0,6,7,8,9,10,mm\r\n",replaced,detail),"duplicate class IDs parse");
    Check(replaced.Find(0)->increases==std::array<int,5>{6,7,8,9,10}&&
        replaced.Find(0)->order==std::vector<UpgradeStat>{UpgradeStat::Ammo,UpgradeStat::Ammo},"later class replaces default and earlier entry without deduplicating order");
    // A physical row's first column is descriptive, not a key. Keep expected
    // values independent of the parser and deliberately reverse those labels.
    std::string plain="id,xp,unused,h,d,s,a,m\r\n";
    for(int i=0;i<=100;++i)plain+=std::to_string(100-i)+","+std::to_string(500+i)+",x,100,1,2,3,4\r\n";
    UpgradeLevels physical;Require(ReadUpgradeLevels(plain,physical,detail),"mislabelled physical-row fixture parses");
    Check(physical.rows.front().experience==500&&physical.rows.back().experience==600,"level keys never sort or index physical rows");
    {
        difficulty_test::Files files;files.contents[LevelPath]=difficulty_test::Read(resourceRoot/"ZBS_neededexp.cso");
        files.contents[ClassPath]=difficulty_test::Read(resourceRoot/"ZBS_class.cso");
        UpgradeResources resources;ResourceError error;
        Require(resources.Load(files.Api(),error),"resource snapshot load");auto retained=resources.Snapshot();
        Check(files.order==std::vector<std::string>{std::string("load:")+LevelPath,std::string("free:")+LevelPath,
            std::string("load:")+ClassPath,std::string("free:")+ClassPath},"original paths load and release in order");
        files.contents.erase(ClassPath);
        Check(!resources.Load(files.Api(),error)&&!resources.Snapshot(),"failed next map cannot retain old published snapshot");
        Check(retained->levels.rows[100].experience==7000,"in-flight old snapshot remains owned until cancellation");
    }
}
}
