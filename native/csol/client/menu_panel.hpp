// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vgui_controls/EditablePanel.h>
#include <VGUI/IPanel.h>
#include <VGUI/ISurface.h>

namespace csol::client {

// Host adapter: retain VGUI's actual parent clipping and input hierarchy while
// drawing these children in the existing overlay compositor. The embedded
// panel's ordinary traversal must not paint them a second time underneath it.
class MenuLayerPanel final : public vgui::EditablePanel {
public:
    MenuLayerPanel(vgui::VPANEL parent,const char* name) : vgui::EditablePanel(nullptr,name)
    {
        SetParent(parent);
        SetPaintEnabled(false);
        SetPaintBackgroundEnabled(false);
        SetPaintBorderEnabled(false);
        SetVisible(false);
    }

    vgui::VPANEL GetCurrentKeyFocus() override
    {
        if(!IsVisible()||!IsKeyBoardInputEnabled())return 0;
        const auto focus=vgui::EditablePanel::GetCurrentKeyFocus();
        return focus&&vgui::ipanel()->IsVisible(focus)&&vgui::ipanel()->IsKeyBoardInputEnabled(focus)?focus:0;
    }

    void PaintOverlay()
    {
        if(!IsVisible())return;
        const auto panel=GetVPanel();
        const int count=vgui::ipanel()->GetChildCount(panel);
        for(int index=0;index<count;++index){
            const auto child=vgui::ipanel()->GetChild(panel,index);
            const bool paint=vgui::surface()->ShouldPaintChildPanel(child);
            if(!paint)vgui::surface()->Invalidate(child);
            vgui::ipanel()->PaintTraverse(child,paint,paint);
        }
    }

    // Only the children paint; their native traversal applies all ancestor
    // clipping. Keep this root visible for layout and native input dispatch.
    void PaintTraverse(bool,bool=true) override {}
};

class MenuPaintPanel final : public vgui::Panel {
public:
    MenuPaintPanel(vgui::Panel* parent,const char* name,void (*paint)())
        : vgui::Panel(parent,name),paint_(paint)
    {
        SetPaintBackgroundEnabled(false);
        SetPaintBorderEnabled(false);
        SetMouseInputEnabled(false);
        SetKeyBoardInputEnabled(false);
    }

protected:
    void Paint() override { if(paint_)paint_(); }

private:
    void (*paint_)();
};

} // namespace csol::client
