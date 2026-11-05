// Test-only no-op boundaries from the pinned SDK. Real font/scheme/cache code is linked.
#pragma once
#include <IFileSystem.h>
#include <VGUI/ISurface2.h>
#include <IEngineSurface.h>
namespace vgui {
class SurfaceStub : public ISurface2 { public:
void Shutdown(void) override {}
void RunFrame(void) override {}
VPANEL GetEmbeddedPanel(void) override {return {};}
void SetEmbeddedPanel(VPANEL pPanel) override {}
void PushMakeCurrent(VPANEL panel, bool useInsets) override {}
void PopMakeCurrent(VPANEL panel) override {}
void DrawSetColor(int r, int g, int b, int a) override {}
void DrawSetColor(Color col) override {}
void DrawFilledRect(int x0, int y0, int x1, int y1) override {}
void DrawFilledRectArray(IntRect* pRects, int numRects) override {}
void DrawOutlinedRect(int x0, int y0, int x1, int y1) override {}
void DrawLine(int x0, int y0, int x1, int y1) override {}
void DrawPolyLine(int* px, int* py, int numPoints) override {}
void DrawSetTextFont(HFont font) override {}
void DrawSetTextColor(int r, int g, int b, int a) override {}
void DrawSetTextColor(Color col) override {}
void DrawSetTextPos(int x, int y) override {}
void DrawGetTextPos(int& x, int& y) override {}
void DrawPrintText(const wchar_t* text, int textLen, FontDrawType_t drawType = FONT_DRAW_DEFAULT) override {}
void DrawUnicodeChar(wchar_t wch, FontDrawType_t drawType = FONT_DRAW_DEFAULT) override {}
void DrawFlushText(void) override {}
IHTML* CreateHTMLWindow(IHTMLEvents* events, VPANEL context) override {return {};}
void PaintHTMLWindow(IHTML* htmlwin) override {}
void DeleteHTMLWindow(IHTML* htmlwin) override {}
int DrawGetTextureId(const char* filename) override {return {};}
bool DrawGetTextureFile(int id, char* filename, int maxlen) override {return {};}
void DrawSetTextureFile(int id, const char* filename, int hardwareFilter, bool forceReload) override {}
void DrawSetTextureRGBA(int id, const unsigned char* rgba, int wide, int tall, int hardwareFilter, bool forceReload) override {}
void DrawSetTexture(int id) override {}
void DrawGetTextureSize(int id, int& wide, int& tall) override {}
void DrawTexturedRect(int x0, int y0, int x1, int y1) override {}
void DrawTexturedRectAdd(int x0, int y0, int x1, int y1) override {}
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
bool AddGlyphSetToFont(HFont font, const char* windowsFontName, int tall, int weight, int blur, int scanlines, int flags) override {return {};}
bool AddCustomFontFile(const char* fontFileName) override {return {};}
int GetFontTall(HFont font) override {return {};}
int GetFontAscent(HFont font, wchar_t wch) override {return {};}
bool IsFontAdditive(HFont font) override {return {};}
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
void GetHDProportionalBase(int& width, int& height) override {}
void SetProportionalBase(int width, int height) override {}
void SetHDProportionalBase(int width, int height) override {}
void CalculateMouseVisible(void) override {}
bool NeedKBInput(void) override {return {};}
bool HasCursorPosFunctions(void) override {return {};}
void SurfaceGetCursorPos(int& x, int& y) override {}
void SurfaceSetCursorPos(int x, int y) override {}
void DrawTexturedLine(const Vertex_t& a, const Vertex_t& b) override {}
void DrawOutlinedCircle(int x, int y, int radius, int segments) override {}
void DrawTexturedPolyLine(const Vertex_t* p, int n) override {}
void DrawTexturedSubRect(int x0, int y0, int x1, int y1, float texs0, float text0, float texs1, float text1) override {}
void DrawTexturedPolygon(int n, Vertex_t* pVertices) override {}
const wchar_t* GetTitle(VPANEL panel) override {return {};}
bool IsCursorLocked(void) const override {return {};}
void SetWorkspaceInsets(int left, int top, int right, int bottom) override {}
bool DrawGetUnicodeCharRenderInfo(wchar_t ch, CharRenderInfo& info) override {return {};}
void DrawRenderCharFromInfo(const CharRenderInfo& info) override {}
void DrawSetAlphaMultiplier(float alpha) override {}
float DrawGetAlphaMultiplier(void) override {return {};}
void SetAllowHTMLJavaScript(bool state) override {}
void OnScreenSizeChanged(int nOldWidth, int nOldHeight) override {}
HCursor CreateCursorFromFile(const char* curOrAniFile, const char* pPathID) override {return {};}
void* DrawGetTextureMatInfoFactory(int id) override {return {};}
void PaintTraverseEx(VPANEL panel, bool paintPopups = false) override {}
float GetZPos(void) const override {return {};}
void SetPanelForInput(VPANEL vpanel) override {}
void DrawFilledRectFade(int x0, int y0, int x1, int y1, unsigned int alpha0, unsigned int alpha1, bool bHorizontal) override {}
void DrawSetTextureRGBAEx(int id, const unsigned char* rgba, int wide, int tall, int imageFormat) override {}
void DrawSetTextScale(float sx, float sy) override {}
bool SetBitmapFontGlyphSet(HFont font, const char* windowsFontName, float scalex, float scaley, int flags) override {return {};}
bool AddBitmapFontFile(const char* fontFileName) override {return {};}
void SetBitmapFontName(const char* pName, const char* pFontFilename) override {}
const char* GetBitmapFontName(const char* pName) override {return {};}
void ClearTemporaryFontCache(void) override {}
IImage* GetIconImageForFullPath(const char* pFullPath) override {return {};}
void DrawUnicodeString(const wchar_t* pwString, FontDrawType_t drawType) override {}
void PrecacheFontCharacters(HFont font, wchar_t* pCharacters) override {}
const char* GetResolutionKey(void) const override {return {};}
void DeleteTextureByID(int textureId) override {}
double GetAbsoluteTime() const override {return {};}
void SetForcingHDProportional(bool bForcingHDProportional) override {}
bool IsForcingHDProportional() const override {return {};}
void SetLanguage(const char* pchLang) override {}
const char* GetLanguage() override {return {};}
void DrawUpdateRegionTextureBGRA(int nTextureID, int x, int y, const unsigned char* pchData, int wide, int tall) override {}
void DrawSetTextureBGRA(int id, const unsigned char* pchData, int wide, int tall) override {}
void CreateBrowser(VPANEL panel, IHTMLResponses* pBrowser, bool bPopupWindow, const char* pchUserAgentIdentifier) override {}
void RemoveBrowser(VPANEL panel, IHTMLResponses* pBrowser) override {}
IHTMLChromeController* AccessChromeHTMLController() override {return {};}
bool VGUI2MouseControl(void) override {return {};}
void SetVGUI2MouseControl(bool state) override {}
};
}
class FileSystemStub : public IFileSystem { public:
void Mount(void) override {}
void Unmount(void) override {}
void RemoveAllSearchPaths(void) override {}
void AddSearchPath(const char* pPath, const char* pathID = 0) override {}
bool RemoveSearchPath(const char* pPath) override {return {};}
void RemoveFile(const char* pRelativePath, const char* pathID = 0) override {}
void CreateDirHierarchy(const char* path, const char* pathID = 0) override {}
bool FileExists(const char* pFileName) override {return {};}
bool IsDirectory(const char* pFileName) override {return {};}
FileHandle_t Open(const char* pFileName, const char* pOptions, const char* pathID = 0) override {return {};}
void Close(FileHandle_t file) override {}
void Seek(FileHandle_t file, int pos, FileSystemSeek_t seekType) override {}
unsigned Tell(FileHandle_t file) override {return {};}
unsigned Size(FileHandle_t file) override {return {};}
unsigned Size(const char* pFileName) override {return {};}
long GetFileTime(const char* pFileName) override {return {};}
void FileTimeToString(char* pStrip, int maxCharsIncludingTerminator, long fileTime) override {}
bool IsOk(FileHandle_t file) override {return {};}
void Flush(FileHandle_t file) override {}
bool EndOfFile(FileHandle_t file) override {return {};}
int Read(void* pOutput, int size, FileHandle_t file) override {return {};}
int Write(void const* pInput, int size, FileHandle_t file) override {return {};}
char* ReadLine(char* pOutput, int maxChars, FileHandle_t file) override {return {};}
int FPrintf(FileHandle_t file, char* pFormat, ...) override {return {};}
char* GetReadBuffer(FileHandle_t file, char* pBuffer) override {return {};}
void ReleaseReadBuffer(FileHandle_t file, char* pBuffer) override {}
const char* FindFirst(const char* pWildCard, FileFindHandle_t* pHandle, const char* pathID = 0) override {return {};}
const char* FindNext(FileFindHandle_t handle) override {return {};}
bool FindIsDirectory(FileFindHandle_t handle) override {return {};}
void FindClose(FileFindHandle_t handle) override {}
void GetLocalCopy(const char* pFileName) override {}
const char* GetLocalPath(const char* pFileName, char* pLocalPath, int maxlen) override {return {};}
char* ParseFile(char* data, char* token, bool* wasquoted) override {return {};}
bool FullPathToRelativePath(const char* pFullpath, char* pRelative) override {return {};}
bool GetCurrentDirectory(char* pDirectory, int maxlen) override {return {};}
void PrintOpenedFiles(void) override {}
void SetWarningFunc(void (*pfnWarning)(const char* fmt, ...)) override {}
void SetWarningLevel(FileWarningLevel_t level) override {}
void LogLevelLoadStarted(const char* name) override {}
void LogLevelLoadFinished(const char* name) override {}
int HintResourceNeed(const char* hintlist, int forgetEverything) override {return {};}
int PauseResourcePreloading(void) override {return {};}
int ResumeResourcePreloading(void) override {return {};}
int SetVBuf(FileHandle_t stream, char* buffer, int mode, long size) override {return {};}
void GetInterfaceVersion(char* p, int maxlen) override {}
bool IsFileImmediatelyAvailable(const char* path) override {return {};}
void* WaitForResources(const char* pFileName) override {return {};}
bool GetWaitForResourcesProgress(WaitForResourcesHandle_t handle, float* progress, bool* complete) override {return {};}
void CancelWaitForResources(WaitForResourcesHandle_t handle) override {}
bool IsAppReadyForOfflinePlay(void) override {return {};}
void AddPackFile(const char* pPath, const char* pathID = 0) override {}
void* OpenFromCacheForRead(const char* pFileName, const char* pOptions, const char* pathID = 0) override {return {};}
void AddSearchPathNoWrite(const char* pPath, const char* pathID) override {}
};
class EngineSurfaceStub : public IEngineSurface { public:
void pushMakeCurrent(int* p1, int* p2, int* r, bool translateToScreenSpace) override {}
void popMakeCurrent(void) override {}
void drawFilledRect(int x0, int y0, int x1, int y1) override {}
void drawOutlinedRect(int x0, int y0, int x1, int y1) override {}
void drawLine(int x0, int y0, int x1, int y1) override {}
void drawPolyLine(int* px, int* py, int numPoints) override {}
void drawTexturedPolygon(vgui::VGuiVertex* p, int n) override {}
void drawSetTextureRGBA(int id, const unsigned char* rgba, int wide, int tall, int hardwareFilter, bool forceReload) override {}
void drawSetTexture(int id) override {}
void drawTexturedRect(int x0, int y0, int x1, int y1) override {}
int createNewTextureID(void) override {return {};}
void drawSetColor(int r, int g, int b, int a) override {}
void drawSetTextColor(int r, int g, int b, int a) override {}
void drawSetTextPos(int x, int y) override {}
void drawGetTextPos(int& x, int& y) override {}
void drawPrintChar(int x, int y, int wide, int tall, float s0, float t0, float s1, float t1) override {}
void drawPrintCharAdd(int x, int y, int wide, int tall, float s0, float t0, float s1, float t1) override {}
void drawSetTextureFile(int id, const char* filename, int hardwareFilter, bool forceReload) override {}
void drawGetTextureSize(int id, int& wide, int& tall) override {}
bool isTextureIDValid(int id) override {return {};}
bool drawSetSubTextureRGBA(int textureID, int drawX, int drawY, unsigned const char* rgba, int subTextureWide, int subTextureTall) override {return {};}
void drawFlushText(void) override {}
void resetViewPort(void) override {}
void drawSetTextureBGRA(int id, const unsigned char* rgba, int wide, int tall, int hardwareFilter, bool forceUpload) override {}
void drawUpdateRegionTextureBGRA(int textureID, int drawX, int drawY, unsigned const char* rgba, int subTextureWide, int subTextureTall) override {}
};
