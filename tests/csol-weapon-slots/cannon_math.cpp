// SPDX-License-Identifier: GPL-3.0-or-later
#include "cannon.hpp"
#include <cstring>
#include <iomanip>
#include <iostream>

int main(int argc,char** argv)
{
    std::cout<<std::setprecision(9);
    if(argc!=2)return 2;
    if(!std::strcmp(argv[1],"--area"))
    {
        csol::cannon::Point source,forward,minimum,maximum;
        while(std::cin>>source[0]>>source[1]>>source[2]>>forward[0]>>forward[1]>>forward[2]
            >>minimum[0]>>minimum[1]>>minimum[2]>>maximum[0]>>maximum[1]>>maximum[2])
            std::cout<<csol::cannon::Intersects(source,forward,minimum,maximum)<<'\n';
    }
    else if(!std::strcmp(argv[1],"--damage"))
    {
        float base,distance;
        while(std::cin>>base>>distance)std::cout<<csol::cannon::Damage(base,distance)<<'\n';
    }
    else return 2;
    return std::cin.eof()?0:1;
}
