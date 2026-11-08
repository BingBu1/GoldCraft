// SPDX-License-Identifier: GPL-3.0-or-later
#include "transfer.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>
#include <stdexcept>
using namespace parallel_download;
using namespace std::chrono;
unsigned checks=0;
void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void Wait(const std::shared_ptr<Job>& j){const auto until=steady_clock::now()+seconds(10);while(!j->done.load(std::memory_order_acquire)&&steady_clock::now()<until)std::this_thread::sleep_for(milliseconds(10));Check(j->done,"job hung");}
int main(int argc,char** argv){
    try{
        if(argc!=3)throw std::runtime_error("usage: tests URL temp-directory");
        for(auto path:{"models/v_test.mdl","sound/vox/hello world.wav","resource/a.txt"})Check(ValidPath(path),"valid path rejected");
        for(auto path:{"","../a","a/../b","a//b","/a","a\\b","C:a","a?x","a%2fb","a.","con.wav","x/LPT1.txt","a/b ","a/*"})Check(!ValidPath(path),"unsafe path accepted");
        std::filesystem::path root=argv[2];std::filesystem::create_directories(root);
        Limits limits;limits.workers=4;limits.timeout_ms=350;limits.total_ms=2000;limits.max_bytes=1024*1024;
        Pool pool(limits);unsigned id=0;
        auto submit=[&](const char* path){auto j=std::make_shared<Job>();j->url=std::string(argv[1])+path;j->temporary=root/(std::to_string(++id)+".part");Check(pool.Submit(j),"submit failed");return j;};
        std::vector<std::shared_ptr<Job>> jobs;
        auto start=steady_clock::now();for(int i=0;i<8;++i)jobs.push_back(submit("ok"));
        for(auto& j:jobs){Wait(j);Check(j->success,"HTTP200 failed");Check(j->received==65536,"size wrong");Check(std::filesystem::file_size(j->temporary)==65536,"disk size wrong");std::ifstream f(j->temporary,std::ios::binary);std::string bytes(65536,'\0');f.read(bytes.data(),bytes.size());Check(bytes==std::string(65536,'x'),"body mismatch");}
        Check(steady_clock::now()-start<milliseconds(1300),"requests did not overlap");
        auto missing=submit("missing"),truncated=submit("truncated"),hang=submit("hang"),large=submit("large"),redirect=submit("redirect"),chunked=submit("chunked");
        for(auto& j:{missing,truncated,hang,large,redirect}){Wait(j);Check(!j->success,"bad response accepted");}
        Check(missing->status==404&&missing->attempts==1,"404 retried");Check(hang->attempts==2,"retry bound wrong");
        Wait(chunked);Check(chunked->success&&chunked->received==65536,"chunked response failed");
        auto cancelled=submit("hang");std::this_thread::sleep_for(milliseconds(50));start=steady_clock::now();pool.Cancel();Wait(cancelled);Check(!cancelled->success&&cancelled->cancelled,"cancel ignored");Check(steady_clock::now()-start<milliseconds(500),"cancel blocked");
        auto next=submit("ok");Wait(next);Check(next->success,"next batch failed");
        auto temp=next->temporary;next.reset();for(int i=0;i<100&&std::filesystem::exists(temp);++i)std::this_thread::sleep_for(milliseconds(5));Check(!std::filesystem::exists(temp),"temp not cleaned");
        std::cout<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";return 1;}
}
