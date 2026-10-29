// Test-only no-op implementations of the matched public VGUI interface.
#pragma once
#include <Color.h>
#include <VGUI/ISurface.h>
namespace vgui { class TestSurface : public ISurface { public:
void Shutdown(void) override {}
void RunFrame(void) override {}
VPANEL GetEmbeddedPanel(void) override {return {};}
void SetEmbeddedPanel(VPANEL pPanel) override {}
void PushMakeCurrent(VPANEL panel, bool useInsets) override {}
void PopMakeCurrent(VPANEL panel) override {}
void DrawSetColor(int r, int g, int b, int a) override {}
void DrawSetColor(Color col) override {}
void DrawFilledRect(int x0, int y0, int x1, int y1) override {}
void DrawOutlinedRect(int x0, int y0, int x1, int y1) override {}
void DrawLine(int x0, int y0, int x1, int y1) override {}
void DrawPolyLine(int* px, int* py, int numPoints) override {}
void DrawSetTextFont(HFont font) override {}
void DrawSetTextColor(int r, int g, int b, int a) override {}
void DrawSetTextColor(Color col) override {}
void DrawSetTextPos(int x, int y) override {}
void DrawGetTextPos(int& x, int& y) override {}
void DrawPrintText(const wchar_t* text, int textLen) override {}
void DrawUnicodeChar(wchar_t wch) override {}
void DrawUnicodeCharAdd(wchar_t wch) override {}
void DrawFlushText(void) override {}
IHTML* CreateHTMLWindow(IHTMLEvents* events, VPANEL context) override {return {};}
void PaintHTMLWindow(IHTML* htmlwin) override {}
void DeleteHTMLWindow(IHTML* htmlwin) override {}
void DrawSetTextureFile(int id, const char* filename, int hardwareFilter, bool forceReload) override {}
void DrawSetTextureRGBA(int id, const unsigned char* rgba, int wide, int tall, int hardwareFilter, bool forceReload) override {}
void DrawSetTexture(int id) override {}
void DrawGetTextureSize(int id, int& wide, int& tall) override {}
void DrawTexturedRect(int x0, int y0, int x1, int y1) override {}
bool IsTextureIDValid(int id) override {return {};}
int CreateNewTextureID(bool procedural = false) override {return {};}
void GetScreenSize(int& wide, int& tall) override {}
void SetAsTopMost(VPANEL panel, bool state) override {}
void BringToFront(VPANEL panel) override {}
void SetForegroundWindow(VPANEL panel) override {}
void SetPanelVisible(VPANEL panel, bool state) override {}
void SetMinimized(VPANEL panel, bool state) override {}
bool IsMinimized(VPANEL panel) override {return {};}
void FlashWindow(VPANEL panel, bool state) override {}
void SetTitle(VPANEL panel, const wchar_t* title) override {}
void SetAsToolBar(VPANEL panel, bool state) override {}
void CreatePopup(VPANEL panel, bool minimised, bool showTaskbarIcon = true, bool disabled = false, bool mouseInput = true, bool kbInput = true) override {}
void SwapBuffers(VPANEL panel) override {}
void Invalidate(VPANEL panel) override {}
void SetCursor(HCursor cursor) override {}
bool IsCursorVisible(void) override {return {};}
void ApplyChanges(void) override {}
bool IsWithin(int x, int y) override {return {};}
bool HasFocus(void) override {return {};}
bool SupportsFeature(SurfaceFeature_e feature) override {return {};}
void RestrictPaintToSinglePanel(VPANEL panel) override {}
void SetModalPanel(VPANEL panel) override {}
VPANEL GetModalPanel(void) override {return {};}
void UnlockCursor(void) override {}
void LockCursor(void) override {}
void SetTranslateExtendedKeys(bool state) override {}
VPANEL GetTopmostPopup(void) override {return {};}
void SetTopLevelFocus(VPANEL panel) override {}
HFont CreateFont(void) override {return {};}
bool AddGlyphSetToFont(HFont font, const char* windowsFontName, int tall, int weight, int blur, int scanlines, int flags, int lowRange, int highRange) override {return {};}
bool AddCustomFontFile(const char* fontFileName) override {return {};}
int GetFontTall(HFont font) override {return {};}
void GetCharABCwide(HFont font, int ch, int& a, int& b, int& c) override {}
int GetCharacterWidth(HFont font, int ch) override {return {};}
void GetTextSize(HFont font, const wchar_t* text, int& wide, int& tall) override {}
VPANEL GetNotifyPanel(void) override {return {};}
void SetNotifyIcon(VPANEL context, HTexture icon, VPANEL panelToReceiveMessages, const char* text) override {}
void PlaySound(const char* fileName) override {}
int GetPopupCount(void) override {return {};}
VPANEL GetPopup(int index) override {return {};}
bool ShouldPaintChildPanel(VPANEL childPanel) override {return {};}
bool RecreateContext(VPANEL panel) override {return {};}
void AddPanel(VPANEL panel) override {}
void ReleasePanel(VPANEL panel) override {}
void MovePopupToFront(VPANEL panel) override {}
void MovePopupToBack(VPANEL panel) override {}
void SolveTraverse(VPANEL panel, bool forceApplySchemeSettings = false) override {}
void PaintTraverse(VPANEL panel) override {}
void EnableMouseCapture(VPANEL panel, bool state) override {}
void GetWorkspaceBounds(int& x, int& y, int& wide, int& tall) override {}
void GetAbsoluteWindowBounds(int& x, int& y, int& wide, int& tall) override {}
void GetProportionalBase(int& width, int& height) override {}
void CalculateMouseVisible(void) override {}
bool NeedKBInput(void) override {return {};}
bool HasCursorPosFunctions(void) override {return {};}
void SurfaceGetCursorPos(int& x, int& y) override {}
void SurfaceSetCursorPos(int x, int y) override {}
void DrawTexturedPolygon(vgui::VGuiVertex* p, int n) override {}
int GetFontAscent(HFont font, wchar_t wch) override {return {};}
void SetAllowHTMLJavaScript(bool state) override {}
void SetLanguage(const char* szLanguage) override {}
const char* GetLanguage(void) override {return {};}
bool DeleteTextureByID(int id) override {return {};}
void DrawUpdateRegionTextureBGRA(int nTextureID, int x, int y, const unsigned char* pchData, int wide, int tall) override {}
void DrawSetTextureBGRA(int id, const unsigned char* rgba, int wide, int tall) override {}
void CreateBrowser(VPANEL panel, IHTMLResponses* pBrowser, bool bPopupWindow, const char* pchUserAgentIdentifier) override {}
void RemoveBrowser(VPANEL panel, IHTMLResponses* pBrowser) override {}
IHTMLChromeController* AccessChromeHTMLController(void) override {return {};}
}; }
