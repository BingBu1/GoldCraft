// Read-only WASAPI metering for explicitly selected sandbox processes.
// This does not record audio, access microphones, or change endpoint/session volume.
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
struct Sample {unsigned seen=0,active=0,nonzero=0;float peak=0,volume=0;BOOL muted=FALSE;std::vector<double> energy;std::vector<float> channelPeak;};

int main(int argc,char** argv){
    if(argc<3){std::cerr<<"Usage: goldcraft_audio_probe SECONDS PID [PID...]\n";return 2;}
    const double seconds=std::atof(argv[1]);
    if(!std::isfinite(seconds)||seconds<=0||seconds>30)return 2;
    std::map<DWORD,Sample> samples;
    for(int i=2;i<argc;++i){auto pid=std::strtoul(argv[i],nullptr,10);if(!pid)return 2;samples.emplace(pid,Sample{});}
    const HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(init))return 3;
    int result=[&]{
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioEndpointVolume> endpoint;
        HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator));
        if(SUCCEEDED(hr))hr=enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device);
        if(SUCCEEDED(hr))hr=device->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,&manager);
        if(FAILED(hr)){std::cerr<<"WASAPI initialization HRESULT="<<std::hex<<hr<<'\n';return 4;}
        BOOL endpointMuted=FALSE;float endpointVolume=-1;
        if(SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,&endpoint))){endpoint->GetMute(&endpointMuted);endpoint->GetMasterVolumeLevelScalar(&endpointVolume);}
        const auto start=std::chrono::steady_clock::now();unsigned observations=0;
        while(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<seconds){
            ComPtr<IAudioSessionEnumerator> sessions;
            if(FAILED(manager->GetSessionEnumerator(&sessions)))return 5;
            int count=0;sessions->GetCount(&count);++observations;
            for(int i=0;i<count;++i){
                ComPtr<IAudioSessionControl> control;ComPtr<IAudioSessionControl2> identity;
                if(FAILED(sessions->GetSession(i,&control))||FAILED(control.As(&identity)))continue;
                DWORD pid=0;if(FAILED(identity->GetProcessId(&pid))||!samples.contains(pid))continue;
                auto& s=samples[pid];++s.seen;AudioSessionState state;
                if(SUCCEEDED(control->GetState(&state))&&state==AudioSessionStateActive)++s.active;
                ComPtr<ISimpleAudioVolume> volume;if(SUCCEEDED(control.As(&volume))){volume->GetMute(&s.muted);volume->GetMasterVolume(&s.volume);}
                ComPtr<IAudioMeterInformation> meter;if(FAILED(control.As(&meter)))continue;
                float peak=0;if(SUCCEEDED(meter->GetPeakValue(&peak))){s.peak=std::max(s.peak,peak);if(peak>0.00001f)++s.nonzero;}
                UINT channels=0;if(FAILED(meter->GetMeteringChannelCount(&channels))||channels==0||channels>32)continue;
                std::vector<float> levels(channels);
                if(FAILED(meter->GetChannelsPeakValues(channels,levels.data())))continue;
                s.energy.resize(channels);s.channelPeak.resize(channels);
                for(unsigned c=0;c<channels;++c){s.energy[c]+=double(levels[c])*levels[c];s.channelPeak[c]=std::max(s.channelPeak[c],levels[c]);}
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        auto array=[](const auto& values){std::cout<<'[';bool first=true;for(auto v:values){if(!first)std::cout<<',';first=false;std::cout<<v;}std::cout<<']';};
        std::cout<<"{\"seconds\":"<<seconds<<",\"observations\":"<<observations<<",\"endpointMuted\":"<<(endpointMuted?"true":"false")<<",\"endpointVolume\":"<<endpointVolume<<",\"processes\":{";
        bool first=true;for(const auto& [pid,s]:samples){
            if(!first)std::cout<<',';first=false;
            std::cout<<'\"'<<pid<<"\":{\"sessionSamples\":"<<s.seen<<",\"activeSamples\":"<<s.active<<",\"nonzeroSamples\":"<<s.nonzero<<",\"peak\":"<<s.peak<<",\"muted\":"<<(s.muted?"true":"false")<<",\"volume\":"<<s.volume<<",\"channelEnergy\":";array(s.energy);std::cout<<",\"channelPeak\":";array(s.channelPeak);std::cout<<'}';
        }
        std::cout<<"}}\n";return 0;
    }();
    CoUninitialize();return result;
}
