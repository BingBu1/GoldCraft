// Offline RPC adapter around the pinned, unmodified AMXX Pawn interpreter.
// Engine natives are supplied by test_bytecode.py; no game process is launched.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "amx.h"

static void memory(AMX* amx, const std::string& line) {
    std::istringstream in(line);
    std::string op; int address, count; in >> op >> address >> count;
    cell* p = nullptr;
    if (count < 0 || count > 4096 || amx_GetAddr(amx, address, &p)) std::exit(7);
    if (op == "get") {
        std::cout << "M";
        for (int i=0;i<count;++i) std::cout << ' ' << p[i];
        std::cout << std::endl;
    } else {
        for (int i=0;i<count;++i) in >> p[i];
        std::cout << "W" << std::endl;
    }
}
static int AMXAPI callback(AMX* amx, cell index, cell* result, cell* args) {
    char name[256]{}; amx_GetNative(amx,index,name);
    std::cout << "N " << name;
    for (int i=1;i<=args[0]/4;++i) std::cout << ' ' << args[i];
    std::cout << std::endl;
    std::string line;
    while (std::getline(std::cin,line)) {
        if (line.rfind("ret ",0)==0) { *result=static_cast<cell>(std::stoll(line.substr(4))); return 0; }
        if (line.rfind("get ",0)==0 || line.rfind("set ",0)==0) memory(amx,line);
        else return AMX_ERR_NATIVE;
    }
    return AMX_ERR_NATIVE;
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    std::ifstream f(argv[1],std::ios::binary);
    std::vector<unsigned char> image((std::istreambuf_iterator<char>(f)),{});
    if(image.size()<sizeof(AMX_HEADER)) return 3;
    const auto size=reinterpret_cast<AMX_HEADER*>(image.data())->stp;
    image.resize(size+4096);
    AMX amx{}; int error=amx_Init(&amx,image.data());
    if(error) { std::cout << "ERR init " << error << std::endl; return 4; }
    amx.callback=callback; amx.sysreq_d=0; amx.flags |= AMX_FLAG_NTVREG;
    std::cout << "READY" << std::endl;
    std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream in(line); std::string op,name; in>>op>>name;
        if(op=="quit") break;
        if(op=="var") {
            cell address,value; in>>value;
            if(amx_FindPubVar(&amx,name.c_str(),&address)) return 5;
            cell* p; amx_GetAddr(&amx,address,&p); *p=value; std::cout<<"V"<<std::endl;
        } else if(op=="call") {
            std::vector<cell> args; cell v; while(in>>v) args.push_back(v);
            int fn; if(amx_FindPublic(&amx,name.c_str(),&fn)) return 6;
            for(auto it=args.rbegin();it!=args.rend();++it) amx_Push(&amx,*it);
            cell result; error=amx_Exec(&amx,&result,fn);
            std::cout<<"R "<<error<<' '<<result<<std::endl;
        }
    }
    amx_Cleanup(&amx);
}
