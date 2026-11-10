// SPDX-License-Identifier: GPL-3.0-or-later
// Controlled test implementations of the pinned Valve VGUI interfaces.
// Interface declarations remain in the upstream headers; no ABI substitution.
#pragma once
#include <VGUI/IInput2.h>
#include <VGUI/ISystem.h>
#include <VGUI/ILocalize.h>
#include <VGUI/IScheme2.h>
namespace vgui {
struct InputBoundary : IInput2 {
void SetMouseFocus(VPANEL newMouseFocus) override {}
void SetMouseCapture(VPANEL panel) override {}
void GetKeyCodeText(KeyCode code, char *buf, int buflen) override {}
VPANEL GetFocus(void) override {return {};}
VPANEL GetMouseOver(void) override {return {};}
void SetCursorPos(int x, int y) override {}
void GetCursorPos(int &x, int &y) override {}
bool WasMousePressed(MouseCode code) override {return {};}
bool WasMouseDoublePressed(MouseCode code) override {return {};}
bool IsMouseDown(MouseCode code) override {return {};}
void SetCursorOveride(HCursor cursor) override {}
HCursor GetCursorOveride(void) override {return {};}
bool WasMouseReleased(MouseCode code) override {return {};}
bool WasKeyPressed(KeyCode code) override {return {};}
bool IsKeyDown(KeyCode code) override {return {};}
bool WasKeyTyped(KeyCode code) override {return {};}
bool WasKeyReleased(KeyCode code) override {return {};}
VPANEL GetAppModalSurface(void) override {return {};}
void SetAppModalSurface(VPANEL panel) override {}
void ReleaseAppModalSurface(void) override {}
void GetCursorPosition(int &x, int &y) override {}
void RunFrame(void) override {}
void UpdateMouseFocus(int x, int y) override {}
void PanelDeleted(VPANEL panel) override {}
bool InternalCursorMoved(int x, int y) override {return {};}
bool InternalMousePressed(MouseCode code) override {return {};}
bool InternalMouseDoublePressed(MouseCode code) override {return {};}
bool InternalMouseReleased(MouseCode code) override {return {};}
bool InternalMouseWheeled(int delta) override {return {};}
bool InternalKeyCodePressed(KeyCode code) override {return {};}
void InternalKeyCodeTyped(KeyCode code) override {}
void InternalKeyTyped(wchar_t unichar) override {}
bool InternalKeyCodeReleased(KeyCode code) override {return {};}
HInputContext CreateInputContext(void) override {return {};}
void DestroyInputContext(HInputContext context) override {}
void AssociatePanelWithInputContext(HInputContext context, VPANEL pRoot) override {}
void ActivateInputContext(HInputContext context) override {}
VPANEL GetMouseCapture(void) override {return {};}
bool IsChildOfModalPanel(VPANEL panel) override {return {};}
void ResetInputContext(HInputContext context) override {}
void SetIMEWindow( void *hwnd ) override {}
void *GetIMEWindow() override {return {};}
void OnChangeIME( bool forward ) override {}
int GetCurrentIMEHandle() override {return {};}
int GetEnglishIMEHandle() override {return {};}
void GetIMELanguageName( wchar_t *buf, int unicodeBufferSizeInBytes ) override {}
void GetIMELanguageShortCode( wchar_t *buf, int unicodeBufferSizeInBytes ) override {}
int GetIMELanguageList( LanguageItem *dest, int destcount ) override {return {};}
int GetIMEConversionModes( ConversionModeItem *dest, int destcount ) override {return {};}
int GetIMESentenceModes( SentenceModeItem *dest, int destcount ) override {return {};}
void OnChangeIMEByHandle( int handleValue ) override {}
void OnChangeIMEConversionModeByHandle( int handleValue ) override {}
void OnChangeIMESentenceModeByHandle( int handleValue ) override {}
void OnInputLanguageChanged() override {}
void OnIMEStartComposition() override {}
void OnIMECompositionWin32( long flags ) override {}
void OnIMEEndComposition() override {}
void OnIMEShowCandidates() override {}
void OnIMEChangeCandidates() override {}
void OnIMECloseCandidates() override {}
void OnIMERecomputeModes() override {}
int GetCandidateListCount() override {return {};}
void GetCandidate( int num, wchar_t *dest, int destSizeBytes ) override {}
int GetCandidateListSelectedItem() override {return {};}
int GetCandidateListPageSize() override {return {};}
int GetCandidateListPageStart() override {return {};}
void SetCandidateWindowPos( int x, int y ) override {}
bool GetShouldInvertCompositionString() override {return {};}
bool CandidateListStartsAtOne() override {return {};}
void SetCandidateListPageStart( int start ) override {}
void GetCompositionString(wchar_t *dest, int destSizeBytes) override {}
void OnIMESelectCandidate(int num) override {}
void PostKeyMessage(KeyValues *message) override {}
void DestroyCandidateList(void) override {}
void CreateNewCandidateListWin32(void) override {}
void InternalShowCandidateWindow(void) override {}
void InternalHideCandidateWindow(void) override {}
void InternalUpdateCandidateWindow(void) override {}
void InternalSetCompositionString(const wchar_t *compstr) override {}
void OnKeyCodeUnhandled(int keyCode) override {}
bool ShouldModalSubTreeReceiveMessages() const override {return {};}
VPANEL GetModalSubTree(void) override {return {};}
bool IsIMEComposing() const override {return {};}
double GetImeComposingTime() const override {return {};}
void CancelIMEComposition() override {}
};
struct SchemeBoundary : IScheme2 {
const char *GetResourceString(const char *stringName) override {return {};}
IBorder *GetBorder(const char *borderName) override {return {};}
HFont GetFont(const char *fontName, bool proportional = false) override {return {};}
Color GetColor(const char *colorName, Color defaultColor) override {return {};}
HFont GetHDFont(const char* fontName, bool proportional, bool hd) override {return {};}
void Shutdown(bool full) override {}
void LoadFromFile(VPANEL sizingPanel, const char* filename, const char* tag, KeyValues* inKeys) override {}
const char* GetName(void) override {return {};}
const char* GetFileName(void) override {return {};}
char const* GetFontName(const HFont& font) override {return {};}
void ReloadFontGlyphs(void) override {}
VPANEL GetSizingPanel(void) override {return {};}
};
struct SchemeManagerBoundary : ISchemeManager2 {
HScheme LoadSchemeFromFile(const char *fileName, const char *tag) override {return {};}
void ReloadSchemes(void) override {}
HScheme GetDefaultScheme(void) override {return {};}
HScheme GetScheme(const char *tag) override {return {};}
IImage *GetImage(const char *imageName, bool hardwareFiltered) override {return {};}
HTexture GetImageID(const char *imageName, bool hardwareFiltered) override {return {};}
IScheme *GetIScheme(HScheme scheme) override {return {};}
void Shutdown(bool full = true) override {}
int GetProportionalScaledValue(int normalizedValue) override {return {};}
int GetProportionalNormalizedValue(int scaledValue) override {return {};}
float GetProportionalScale(void) override {return {};}
int GetHDProportionalScaledValue(int normalizedValue) override {return {};}
int GetHDProportionalNormalizedValue(int scaledValue) override {return {};}
int GetProportionalScaledValueEx(HScheme scheme, int normalizedValue) override {return {};}
int GetProportionalNormalizedValueEx(HScheme scheme, int scaledValue) override {return {};}
int GetProportionalScaledValueEx(IScheme2* pScheme, int normalizedValue) override {return {};}
int GetProportionalNormalizedValueEx(IScheme2* pScheme, int scaledValue) override {return {};}
int GetHDProportionalScaledValueEx(HScheme scheme, int normalizedValue) override {return {};}
int GetHDProportionalNormalizedValueEx(HScheme scheme, int scaledValue) override {return {};}
int GetHDProportionalScaledValueEx(IScheme2* pScheme, int normalizedValue) override {return {};}
int GetHDProportionalNormalizedValueEx(IScheme2* pScheme, int scaledValue) override {return {};}
HScheme LoadSchemeFromFileEx(VPANEL sizingPanel, const char* fileName, const char* tag) override {return {};}
float GetHorizontalProportionalScale(void) override {return {};}
int GetHorizontalProportionalScaledValue(int normalizedValue) override {return {};}
int GetHorizontalProportionalNormalizedValue(int scaledValue) override {return {};}
int GetHDHorizontalProportionalScaledValue(int normalizedValue) override {return {};}
int GetHDHorizontalProportionalNormalizedValue(int scaledValue) override {return {};}
float GetAlteredProportionalScale(void) override {return {};}
int GetAlteredProportionalScaledValue(int normalizedValue) override {return {};}
int GetAlteredProportionalNormalizedValue(int scaledValue) override {return {};}
int GetHDAlteredProportionalScaledValue(int normalizedValue) override {return {};}
int GetHDAlteredProportionalNormalizedValue(int scaledValue) override {return {};}
void SetForcingAlteredProportional(bool bForcingAlteredProportional) override {}
bool IsForcingAlteredProportional() const override {return {};}
};
struct SystemBoundary : ISystem {
void Shutdown(void) override {}
void RunFrame(void) override {}
void ShellExecute(const char *command, const char *file) override {}
double GetFrameTime(void) override {return {};}
double GetCurrentTime(void) override {return {};}
long GetTimeMillis(void) override {return {};}
int GetClipboardTextCount(void) override {return {};}
void SetClipboardText(const char *text, int textLen) override {}
void SetClipboardText(const wchar_t *text, int textLen) override {}
int GetClipboardText(int offset, char *buf, int bufLen) override {return {};}
int GetClipboardText(int offset, wchar_t *buf, int bufLen) override {return {};}
bool SetRegistryString(const char *key, const char *value) override {return {};}
bool GetRegistryString(const char *key, char *value, int valueLen) override {return {};}
bool SetRegistryInteger(const char *key, int value) override {return {};}
bool GetRegistryInteger(const char *key, int &value) override {return {};}
KeyValues *GetUserConfigFileData(const char *dialogName, int dialogID) override {return {};}
void SetUserConfigFile(const char *fileName, const char *pathName) override {}
void SaveUserConfigFile(void) override {}
bool SetWatchForComputerUse(bool state) override {return {};}
double GetTimeSinceLastUse(void) override {return {};}
int GetAvailableDrives(char *buf, int bufLen) override {return {};}
bool CommandLineParamExists(const char *paramName) override {return {};}
const char *GetFullCommandLine(void) override {return {};}
bool GetCurrentTimeAndDate(int *year, int *month, int *dayOfWeek, int *day, int *hour, int *minute, int *second) override {return {};}
double GetFreeDiskSpace(const char *path) override {return {};}
bool CreateShortcut(const char *linkFileName, const char *targetPath, const char *arguments, const char *workingDirectory, const char *iconFile) override {return {};}
bool GetShortcutTarget(const char *linkFileName, char *targetPath, char *arguments, int destBufferSizes) override {return {};}
bool ModifyShortcutTarget(const char *linkFileName, const char *targetPath, const char *arguments, const char *workingDirectory) override {return {};}
bool GetCommandLineParamValue(const char *paramName, char *value, int valueBufferSize) override {return {};}
bool DeleteRegistryKey(const char *keyName) override {return {};}
const char *GetDesktopFolderPath(void) override {return {};}
};
struct LocalizeBoundary : ILocalize {
bool AddFile(IFileSystem *fileSystem, const char *fileName) override {return {};}
void RemoveAll(void) override {}
wchar_t *Find(char const *tokenName) override {return {};}
int ConvertANSIToUnicode(const char *ansi, wchar_t *unicode, int unicodeBufferSizeInBytes) override {return {};}
int ConvertUnicodeToANSI(const wchar_t *unicode, char *ansi, int ansiBufferSize) override {return {};}
StringIndex_t FindIndex(const char *tokenName) override {return {};}
void ConstructString(wchar_t *unicodeOuput, int unicodeBufferSizeInBytes, const wchar_t *formatString, int numFormatParameters, ...) override {}
const char *GetNameByIndex(StringIndex_t index) override {return {};}
wchar_t *GetValueByIndex(StringIndex_t index) override {return {};}
StringIndex_t GetFirstStringIndex(void) override {return {};}
StringIndex_t GetNextStringIndex(StringIndex_t index) override {return {};}
void AddString(const char *tokenName, wchar_t *unicodeString, const char *fileName) override {}
void SetValueByIndex(StringIndex_t index, wchar_t *newValue) override {}
bool SaveToFile(IFileSystem *fileSystem, const char *fileName) override {return {};}
int GetLocalizationFileCount(void) override {return {};}
const char *GetLocalizationFileName(int index) override {return {};}
const char *GetFileNameByIndex(StringIndex_t index) override {return {};}
void ReloadLocalizationFiles(void) override {}
void ConstructString(wchar_t *unicodeOutput, int unicodeBufferSizeInBytes, const char *tokenName, KeyValues *localizationVariables) override {}
void ConstructString(wchar_t *unicodeOutput, int unicodeBufferSizeInBytes, StringIndex_t unlocalizedTextSymbol, KeyValues *localizationVariables) override {}
};
}
