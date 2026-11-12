// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <iosfwd>
struct usercmd_s;
namespace csol::client {
void InitializeBuyMenu();
void ShutdownBuyMenu();
void ResetBuyMenu();
void BuyMenuFrame();
void BuyMenuCreateMove(usercmd_s* cmd,int active);
bool BuyMenuVisible();
// Explicit cold request for an authored guest card; no menu is opened.
bool PrepareBuyMenuItem(int id);
void CloseBuyMenuForOverlay();
void BuyMenuDiagnostics(std::ostream& output);
}
