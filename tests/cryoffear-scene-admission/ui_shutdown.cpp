// SPDX-License-Identifier: GPL-3.0-or-later
// Actual map-vote retirement bodies against the matched SDK callback interface.
#include <IVGUI2Extension.h>
#include <cstdio>
unsigned checks{},failures{},invalidReads{},deletions{},resets{},baseUnregisters{},clientUnregisters{};
bool vguiAlive=true,registered=true,clientLifetimeRegistered=true;
int inputValue{},surfaceValue{},uiValue{};
int *input=&inputValue,*surface=&surfaceValue,*clientUI=&uiValue;
unsigned font=1;
struct Panel{void MarkForDeletion(){if(!vguiAlive)++invalidReads;else ++deletions;}}panel;
struct Lease {
    Panel* value=&panel;
    explicit operator bool()const{if(value&&!vguiAlive)++invalidReads;return value&&vguiAlive;}
    Panel* operator->()const{if(!vguiAlive)++invalidReads;return value;}
    void operator=(Panel* next){value=next;}
}listPanel;
struct Extension{
    void UnregisterBaseUICallbacks(void*){++baseUnregisters;}
    void UnregisterClientVGUICallbacks(void*){++clientUnregisters;}
}extensionValue,*extension=&extensionValue;
int overlay{};
bool loaded=true;
void ResetScenarioMapVote(){++resets;if(loaded&&!vguiAlive)++invalidReads;loaded=false;}
#include "ui-lifetime.inc"
#include "ui-public-shutdown.inc"
void Check(bool ok){++checks;if(!ok)++failures;}
int main(){
    // Matching VGUI2Extension: ClientVGUI_Shutdown precedes native unloading.
    clientLifetime.Shutdown();Check(vguiAlive&&deletions==1&&resets==1);
    Check(!listPanel.value&&!surface&&!input&&!clientUI&&!font);
    Check(!baseUnregisters&&!clientUnregisters); // no mutation of iterated callbacks
    vguiAlive=false;ShutdownScenarioMapVote();ShutdownScenarioMapVote();
    Check(!invalidReads&&deletions==1&&baseUnregisters==1&&clientUnregisters==1);
    Check(!registered&&!clientLifetimeRegistered&&!extension);
    // HUD shutdown can also precede native VGUI shutdown and must be idempotent.
    vguiAlive=true;loaded=true;registered=clientLifetimeRegistered=true;extension=&extensionValue;
    listPanel=&panel;surface=&surfaceValue;input=&inputValue;clientUI=&uiValue;font=1;
    ShutdownScenarioMapVote();vguiAlive=false;ShutdownScenarioMapVote();
    Check(!invalidReads&&deletions==2&&baseUnregisters==2&&clientUnregisters==2);
    std::printf("VGUI lifetime checks=%u failures=%u staleInterfaceReads=%u; controlled teardown, live=no\n",checks,failures,invalidReads);
    return failures?1:0;
}
