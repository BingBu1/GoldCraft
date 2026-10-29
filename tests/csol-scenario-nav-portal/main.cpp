// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "../../native/csol/server/scenario_nav_portal.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

bool g_bAllowedCSBot{};
// The real SDK constructor initializes its actual C++ members. Destruction
// only releases those members in this fixture; no global Bot notifications.
CNavArea::~CNavArea() {}
namespace
{
int checks{},failures{};
std::uint32_t Bits(float value){return std::bit_cast<std::uint32_t>(value);}
float Float(std::uint32_t bits){return std::bit_cast<float>(bits);}
void Check(bool value,const char* label)
{++checks;if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
struct Geometry
{
    Vector lo,hi;
};
bool Evaluate(const Geometry& a,const Geometry& b,int direction,Vector& result)
{
    const Vector neA(a.hi.x,a.lo.y,a.lo.z),swA(a.lo.x,a.hi.y,a.hi.z);
    const Vector neB(b.hi.x,b.lo.y,b.lo.z),swB(b.lo.x,b.hi.y,b.hi.z);
    CNavArea from(&a.lo,&neA,&a.hi,&swA),to(&b.lo,&neB,&b.hi,&swB);
    Check(Bits(from.GetExtent()->lo.x)==Bits(a.lo.x) && Bits(from.GetExtent()->hi.y)==Bits(a.hi.y),"real SDK source extent");
    Check(Bits(to.GetExtent()->lo.y)==Bits(b.lo.y) && Bits(to.GetExtent()->hi.x)==Bits(b.hi.x),"real SDK target extent");
    return CSOL_ComputeScenarioPortal(from,to,direction,result);
}
unsigned SeedFor(int residue)
{
    for(unsigned seed=0;seed<10000;++seed){std::srand(seed);if(std::rand()%10==residue)return seed;}
    throw std::runtime_error("CRT seed coverage missing");
}
void Point(const Geometry& a,const Geometry& b,int direction,int residue,float x,float y)
{
    const auto seed=SeedFor(residue);std::srand(seed);const int first=std::rand(),next=std::rand();
    std::srand(seed);Vector result(-123,456,Float(0x7fc12345));
    Check(Evaluate(a,b,direction,result),"valid portal accepted");
    Check(Bits(result.x)==Bits(x) && Bits(result.y)==Bits(y),"explicit edge/range endpoint");
    Check(Bits(result.z)==0x7fc12345,"output Z payload preserved");
    Check(std::rand()==next && first%10==residue,"exactly one CRT random value consumed");
}
void Reject(const Geometry& a,const Geometry& b,int direction)
{
    std::srand(17);const int expected=std::rand();std::srand(17);
    Vector result(Float(0x80000000),-129,Float(0x7fc12345));
    const auto before=std::array{Bits(result.x),Bits(result.y),Bits(result.z)};
    Check(!Evaluate(a,b,direction,result),"invalid geometry or direction rejected");
    Check(std::array{Bits(result.x),Bits(result.y),Bits(result.z)}==before,"reject preserves all output bits");
    Check(std::rand()==expected,"reject consumes no RNG");
}
void Cases()
{
    const Geometry a{{0,20,4},{100,120,8}},b{{10,40,-5},{90,100,7}};
    Point(a,b,NORTH,0,28.75f,20);Point(a,b,NORTH,9,71.25f,20);
    Point(a,b,SOUTH,0,28.75f,120);Point(a,b,SOUTH,9,71.25f,120);
    Point(a,b,EAST,0,10,58.75f);Point(a,b,EAST,9,10,81.25f);
    Point(a,b,WEST,0,90,58.75f);Point(a,b,WEST,9,90,81.25f);
    const Geometry wide{{0,-10,1},{200,300,2}};
    for(float width:std::array{std::nextafter(37.5f,0.0f),37.5f,std::nextafter(37.5f,100.0f)})
    {
        Geometry target{{0,1,0},{width,2,0}};
        Point(wide,target,NORTH,0,width<37.5f?12.5f:18.75f,-10);
    }
    Point(wide,{{0,1,0},{5,2,0}},NORTH,0,12.5f,-10);
    Point(wide,{{0,1,0},{5,2,0}},NORTH,9,-7.5f,-10);
    Point(wide,{{0,1,0},{0,2,0}},NORTH,0,12.5f,-10);
    Point(wide,{{0,1,0},{0,2,0}},NORTH,9,-12.5f,-10);
    Point(a,{{150,40,0},{170,100,0}},NORTH,0,112.5f,20);
    Point(a,{{150,40,0},{170,100,0}},NORTH,9,87.5f,20);
    Point(a,{{-80,40,0},{-20,100,0}},NORTH,0,12.5f,20);
    Point(a,{{-80,40,0},{-20,100,0}},NORTH,9,-12.5f,20);
    for(int direction:std::array{-2147483647,-1,4,2147483647})Reject(a,b,direction);
    const float inf=std::numeric_limits<float>::infinity(),nan=std::numeric_limits<float>::quiet_NaN();
    for(int direction=0;direction<4;++direction)
    {
        const bool alongX=direction==NORTH||direction==SOUTH;
        for(float invalid:std::array{inf,-inf,nan})for(int field=0;field<5;++field)
        {
            auto from=a,to=b;
            float* values[]{alongX?&from.lo.x:&from.lo.y,alongX?&from.hi.x:&from.hi.y,
                alongX?&to.lo.x:&to.lo.y,alongX?&to.hi.x:&to.hi.y,
                alongX?(direction==NORTH?&from.lo.y:&from.hi.y):(direction==WEST?&to.hi.x:&to.lo.x)};
            *values[field]=invalid;Reject(from,to,direction);
        }
        auto from=a,to=b;
        if(alongX)from.lo.x=from.hi.x+1;else from.lo.y=from.hi.y+1;
        Reject(from,to,direction);from=a;
        if(alongX)to.lo.x=to.hi.x+1;else to.lo.y=to.hi.y+1;
        Reject(from,to,direction);
    }
    const float max=std::numeric_limits<float>::max();
    Reject({{-max,0,0},{max,20,0}},{{-max,0,0},{max,20,0}},NORTH);
}
int Oracle()
{
    std::uint32_t direction,seed;std::array<std::uint32_t,15> bits;
    while(std::cin>>direction>>seed)
    {
        for(auto& value:bits)if(!(std::cin>>value))return 2;
        Geometry a{{Float(bits[0]),Float(bits[1]),Float(bits[2])},{Float(bits[3]),Float(bits[4]),Float(bits[5])}};
        Geometry b{{Float(bits[6]),Float(bits[7]),Float(bits[8])},{Float(bits[9]),Float(bits[10]),Float(bits[11])}};
        Vector result(Float(bits[12]),Float(bits[13]),Float(bits[14]));
        std::srand(seed);const int sample=std::rand(),next=std::rand();std::srand(seed);
        const bool accepted=Evaluate(a,b,static_cast<int>(direction),result);
        const int after=std::rand();
        std::cout<<accepted<<' '<<sample<<' '<<next<<' '<<after<<' '<<Bits(result.x)<<' '<<Bits(result.y)<<' '<<Bits(result.z)<<'\n';
    }
    return failures?1:0;
}
}
int main(int argc,char** argv)
{
    static_assert(sizeof(void*)==4 && sizeof(float)==4);
    static_assert(NORTH==0 && EAST==1 && SOUTH==2 && WEST==3);
    if(argc==2 && std::string(argv[1])=="--oracle")return Oracle();
    Cases();std::cout<<"nav_portal checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
