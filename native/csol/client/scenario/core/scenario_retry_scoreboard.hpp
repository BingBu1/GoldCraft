// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "scenario_respawn_ui.hpp"
#include <VGUI/VGUI.h>

namespace csol::scenario_respawn_ui
{
// Original client f4145136: CCSClientScoreBoardDialog owns ZBSRetryCountPanel
// at (420,302,165,68). Constructor029F6F00 creates centered title (7,13,148,20)
// and count (7,42,148,13); it makes no explicit font selection. Exact inherited
// original font and the complete CSOL scoreboard remain separate fidelity work.
inline constexpr Rect OriginalRetryPanel{420, 302, 165, 68};
inline constexpr Rect OriginalRetryTitle{7, 13, 148, 20};
inline constexpr Rect OriginalRetryCount{7, 42, 148, 13};

inline Rect RetryBudgetFooter(const Rect& board, int width, int height)
{
    // The native ClientScoreBoard has a full-size player list. Attach outside
    // its lower edge, retaining the original child geometry instead of
    // covering native rows with the CSOL parent's unrelated coordinates.
    const int wide = OriginalRetryPanel.width, tall = OriginalRetryPanel.height;
    if (board.width <= 0 || board.height <= 0 || width < wide || height < tall) return {};
    const auto bottom = std::int64_t(board.y) + board.height + 4;
    const auto above = std::int64_t(board.y) - tall - 4;
    const auto top = bottom + tall <= height && bottom >= 0 ? bottom : above;
    if (top < 0 || top + tall > height) return {};
    const auto right = std::int64_t(board.x) + board.width - wide;
    return {int(std::clamp<std::int64_t>(right, 0, width - wide)), int(top), wide, tall};
}

class RetryScoreboard final
{
public:
    void Reset() { *this = {}; }

    // Uses the SDK's public interfaces. Templates permit controlled panel-tree
    // lifecycle tests without fake VGUI vtables or private engine offsets.
    template<class Panels, class Gui>
    vgui::VPANEL Visible(Panels& panels, Gui& gui, vgui::VPANEL root, std::uint64_t now)
    {
        if (!root) { Reset(); return 0; }
        if (root_ == vgui::INVALID_PANEL || gui.HandleToPanel(root_) != root)
        {
            Reset(); root_ = gui.PanelToHandle(root);
            if (root_ == vgui::INVALID_PANEL) return 0;
        }
        auto board = panel_ == vgui::INVALID_PANEL ? 0 : gui.HandleToPanel(panel_);
        if (!board || !Named(panels, board) || !UnderRoot(panels, board, root, false))
        {
            panel_ = vgui::INVALID_PANEL;
            if (now < retryAt_) return 0;
            retryAt_ = now + 1000;
            board = Find(panels, root);
            if (!board) return 0;
            panel_ = gui.PanelToHandle(board);
            if (panel_ == vgui::INVALID_PANEL) return 0;
        }
        return UnderRoot(panels, board, root, true) ? board : 0;
    }
private:
    template<class Panels>
    static bool Named(Panels& panels, vgui::VPANEL panel)
    {
        const auto* name = panels.GetName(panel);
        return name && std::string_view(name) == "ClientScoreBoard";
    }
    template<class Panels>
    static bool UnderRoot(Panels& panels, vgui::VPANEL panel, vgui::VPANEL root, bool visible)
    {
        for (unsigned depth = 0; panel && depth < 64; ++depth)
        {
            if (visible && !panels.IsVisible(panel)) return false;
            if (panel == root) return true;
            panel = panels.GetParent(panel);
        }
        return false;
    }
    template<class Panels>
    static vgui::VPANEL Find(Panels& panels, vgui::VPANEL root)
    {
        std::array<vgui::VPANEL, 256> pending{};
        pending[0] = root;
        unsigned count = 1;
        for (unsigned visits = 0; count && visits < 1024; ++visits)
        {
            const auto panel = pending[--count];
            if (Named(panels, panel)) return panel;
            const int children = panels.GetChildCount(panel);
            if (children < 0 || unsigned(children) > pending.size() - count) return 0;
            for (int i = children; i > 0; --i)
                if (const auto child = panels.GetChild(panel, i - 1)) pending[count++] = child;
        }
        return 0;
    }
    vgui::HPanel panel_{vgui::INVALID_PANEL}, root_{vgui::INVALID_PANEL};
    std::uint64_t retryAt_{};
};
}
