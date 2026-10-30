// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "hd_spray.hpp"
#include <vgui_controls/Controls.h>
#include <vgui_controls/Panel.h>
#include <vgui_controls/CheckButton.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/Label.h>
#include <commdlg.h>
#include <VGUI/IPanel.h>
#include <VGUI/IVGui.h>

namespace csol::client {
namespace {
vgui::VPANEL FindChild(vgui::VPANEL parent,const char* name){
    auto* panels=vgui::ipanel();
    for(int i=0;i<panels->GetChildCount(parent);++i){
        const auto child=panels->GetChild(parent,i);
        if(!std::strcmp(panels->GetName(child),name))return child;
        if(const auto found=FindChild(child,name))return found;
    }
    return 0;
}
class Preview final:public vgui::Panel {
public:
    explicit Preview(vgui::Panel* p):Panel(p,"HDPreview"){SetMouseInputEnabled(false);SetKeyBoardInputEnabled(false);}
    std::vector<int> textures;spray::Animation animation;double started{};size_t shown{};
    ~Preview(){Clear();}
    void Clear(){for(auto id:textures)vgui::surface()->DeleteTextureByID(id);textures.clear();animation={};}
    void Load(const spray::Bytes& bytes){Clear();shown=0;Repaint();if(bytes.empty())return;animation=spray::DecodeAnimation(bytes);started=GetTickCount64()*.001;
        for(auto& frame:animation.frames){auto& image=frame.image;const int texture=vgui::surface()->CreateNewTextureID(true);textures.push_back(texture);
            vgui::surface()->DrawSetTextureRGBA(texture,image.RGBA.data(),image.Width,image.Height,1,true);spray::Bytes{}.swap(image.RGBA);}}
    void Frame(){if(textures.size()<2)return;const auto next=spray::FrameAt(animation,GetTickCount64()*.001-started);if(next!=shown){shown=next;Repaint();}}
    void Paint()override{
        for(int y=0;y<GetTall();y+=8)for(int x=0;x<GetWide();x+=8){int c=((x+y)/8)%2?80:110;vgui::surface()->DrawSetColor(c,c,c,255);vgui::surface()->DrawFilledRect(x,y,x+8,y+8);}
        if(textures.empty())return;const unsigned width=animation.First().Width,height=animation.First().Height;
        const float s=(std::min)(float(GetWide())/width,float(GetTall())/height),w=width*s,h=height*s,x=(GetWide()-w)/2,y=(GetTall()-h)/2;
        vgui::surface()->DrawSetTexture(textures[shown]);vgui::surface()->DrawSetColor(255,255,255,255);vgui::Vertex_t v[4];
        v[0].Init({x,y},{0,0});v[1].Init({x+w,y},{1,0});v[2].Init({x+w,y+h},{1,1});v[3].Init({x,y+h},{0,1});vgui::surface()->DrawTexturedPolygon(4,v);
    }
};
class Options final:public vgui::Panel {
    vgui::CheckButton* enabled;vgui::Button* import;vgui::Button* cancelUpload;vgui::Label* status;vgui::DHANDLE<Preview> preview;
    // GameUI controls belong to a different VGUI module. Panel/DHANDLE lookup
    // intentionally cannot resolve them; use generation-checked VPANEL handles.
    vgui::VPanelHandle page,logo,list,color,note,dialog;
    SpraySettings candidate;bool visible{},changed{},error{};
    void FindDialog(){
        for(auto parent=page.Get();parent;parent=vgui::ipanel()->GetParent(parent))
            if(!std::strcmp(vgui::ipanel()->GetName(parent),"OptionsDialog")){dialog=parent;return;}
    }
    void Dirty(){changed=true;FindDialog();if(dialog.Get())vgui::ivgui()->PostMessage(dialog.Get(),new KeyValues("ApplyButtonEnable"),GetVPanel());}
public:
    explicit Options(vgui::Panel* p):Panel(p,"CSOLHDSpray"){
        SetPaintBackgroundEnabled(false);SetBounds(40,187,230,66);
        page=p->GetVPanel();
        logo=FindChild(page.Get(),"LogoImage");list=FindChild(page.Get(),"SpraypaintList");color=FindChild(page.Get(),"SpraypaintColor");note=FindChild(page.Get(),"Label3");
        enabled=new vgui::CheckButton(this,"UseHDSpray","");enabled->SetText(L"使用高清图片喷漆");enabled->SetBounds(0,0,220,20);
        import=new vgui::Button(this,"ImportHDSpray",L"选择图片 / GIF（最大 10 MB）",this,"Import");import->SetBounds(0,22,230,22);
        status=new vgui::Label(this,"HDSprayStatus",L"选择图片后自动缩放");status->SetBounds(0,45,230,21);
        cancelUpload=new vgui::Button(this,"CancelHDTransfer",L"取消上传",this,"CancelUpload");cancelUpload->SetBounds(155,45,75,21);cancelUpload->SetVisible(false);
        preview=new Preview(p);int x=40,y=120,w=64,h=64;if(logo.Get()){vgui::ipanel()->GetPos(logo.Get(),x,y);vgui::ipanel()->GetSize(logo.Get(),w,h);}preview->SetBounds(x,y,w,h);
        FindDialog();
        Reset();
    }
    ~Options(){if(preview.Get())delete preview.Get();}
    void Reset(){candidate=CurrentSpray();enabled->SetSelected(candidate.enabled);preview->Load(candidate.bytes);changed=error=false;UpdateControls();}
    void UpdateControls(){const bool hd=enabled->IsSelected();preview->SetVisible(hd&&!candidate.bytes.empty());if(logo.Get())vgui::ipanel()->SetVisible(logo.Get(),!hd);if(list.Get())vgui::ipanel()->SetEnabled(list.Get(),!hd);if(color.Get())vgui::ipanel()->SetEnabled(color.Get(),!hd);if(note.Get())vgui::ipanel()->SetVisible(note.Get(),false);}
    void Frame(){
        if(!dialog.Get())FindDialog();
        const bool now=dialog.Get()&&vgui::ipanel()->IsVisible(dialog.Get());if(now&&!visible)Reset();visible=now;
        if(enabled->IsSelected()!=candidate.enabled){candidate.enabled=enabled->IsSelected();Dirty();UpdateControls();}
        UpdateControls();
        if(visible&&enabled->IsSelected())preview->Frame();
        const bool sending=IsSprayUploading();cancelUpload->SetVisible(sending);status->SetSize(sending?153:230,21);
        if(!error)status->SetText(changed?L"确定或应用后保存；取消放弃修改":SprayStatus());
    }
    void OnCommand(const char* command)override{
        if(!std::strcmp(command,"CancelUpload")){CancelSprayUpload();Dirty();status->SetText(L"上传已取消，点击应用可重试");error=true;return;}
        if(std::strcmp(command,"Import")){Panel::OnCommand(command);return;}
        wchar_t filename[32768]{};OPENFILENAMEW ofn{};ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=GetActiveWindow();
        ofn.lpstrFilter=L"图片\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.ico;*.webp;*.heic;*.avif\0所有文件\0*.*\0\0";ofn.lpstrFile=filename;ofn.nMaxFile=32768;
        ofn.lpstrTitle=L"选择高清喷漆图片或动态 GIF（最大 10 MB）";ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
        if(!GetOpenFileNameW(&ofn))return;
        try{spray::Dimensions original;auto bytes=spray::EncodeAnimation(ImportSpray(filename,&original));preview->Load(bytes);candidate.bytes=std::move(bytes);candidate.original=original;candidate.enabled=true;enabled->SetSelected(true);Dirty();error=false;UpdateControls();}
        catch(const std::exception& e){ShowError(e.what());}
    }
    void ShowError(const char* text){wchar_t wide[256]{};MultiByteToWideChar(CP_UTF8,0,text,-1,wide,256);status->SetText(wide);error=true;}
    void Apply(){if(!changed)return;try{candidate.enabled=enabled->IsSelected();ApplySpray(candidate);changed=error=false;}catch(const std::exception& e){ShowError(e.what());}}
    bool IsPage(vgui::VPANEL value){return page.Get()==value;}
};
std::vector<vgui::DHANDLE<Options>> panels;
}
void CreateSprayOptions(void* raw){auto* page=static_cast<vgui::Panel*>(raw);if(!page||FindChild(page->GetVPanel(),"CSOLHDSpray"))return;
    panels.emplace_back();panels.back()=new Options(page);}
void ApplySprayOptions(void* raw){auto* page=static_cast<vgui::Panel*>(raw);if(!page)return;for(auto& p:panels)if(p.Get()&&p->IsPage(page->GetVPanel()))p->Apply();}
void SprayOptionsFrame(){for(auto& p:panels)if(p.Get())p->Frame();}
void ReleaseSprayOptions(){for(auto& p:panels)if(p.Get())delete p.Get();panels.clear();}
}
