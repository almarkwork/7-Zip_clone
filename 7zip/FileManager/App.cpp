// App.cpp

#include "StdAfx.h"

#include "resource.h"

#include "Common/StringConvert.h"
#include "Windows/FileDir.h"
#include "Windows/Error.h"
#include "Windows/COM.h"
#include "Windows/Thread.h"
#include "IFolder.h"

#include "App.h"

#include "Resource/CopyDialog/CopyDialog.h"

#include "ExtractCallback.h"
#include "UpdateCallback100.h"
#include "ViewSettings.h"
#include "RegistryUtils.h"

using namespace NWindows;
using namespace NFile;
using namespace NFind;

extern DWORD g_ComCtl32Version;
extern HINSTANCE g_hInstance;

void CPanelCallbackImp::OnTab()
{
  if (g_App.NumPanels != 1)
    _app->Panels[1 - _index].SetFocusToList();  
}

void CPanelCallbackImp::SetFocusToPath(int index)
{ 
  int newPanelIndex = index;
  if (g_App.NumPanels == 1)
    newPanelIndex = g_App.LastFocusedPanel;
  _app->Panels[newPanelIndex]._headerComboBox.SetFocus();
}


void CPanelCallbackImp::OnCopy(UStringVector &externalNames, bool move, bool copyToSame)
  { _app->OnCopy(externalNames, move, copyToSame, _index); }

void CPanelCallbackImp::OnSetSameFolder()
  { _app->OnSetSameFolder(_index); }

void CPanelCallbackImp::OnSetSubFolder()
  { _app->OnSetSubFolder(_index); }

void CPanelCallbackImp::PanelWasFocused()
{ 
  _app->LastFocusedPanel = _index; 
}

void CApp::SetListSettings()
{
  bool showDots = ReadShowDots();
  bool showRealFileIcons = ReadShowRealFileIcons();

  DWORD extendedStyle = LVS_EX_HEADERDRAGDROP;
  if (ReadFullRow())
    extendedStyle |= LVS_EX_FULLROWSELECT;
  if (ReadShowGrid())
    extendedStyle |= LVS_EX_GRIDLINES;
  
  /*
  if (ReadSingleClick())
  {
    extendedStyle |= LVS_EX_ONECLICKACTIVATE 
      | LVS_EX_TRACKSELECT;
    if (ReadUnderline())
      extendedStyle |= LVS_EX_UNDERLINEHOT;
  }
  */

  for (int i = 0; i < kNumPanelsMax; i++)
  {
    Panels[i]._showDots = showDots;
    Panels[i]._showRealFileIcons = showRealFileIcons;
    Panels[i]._exStyle = extendedStyle;
    Panels[i].SetExtendedStyle();
  }
}

void CApp::SetShowSystemMenu()
{
  ShowSystemMenu = ReadShowSystemMenu();
}

void CApp::CreateOnePanel(int panelIndex, const UString &mainPath)
{
  if (PanelsCreated[panelIndex])
    return;
  m_PanelCallbackImp[panelIndex].Init(this, panelIndex);
  UString path;
  if (mainPath.IsEmpty())
  {
    CSysString sysPath; 
    if (!::ReadPanelPath(panelIndex, sysPath))
      sysPath.Empty();
    path = GetUnicodeString(sysPath);
  }
  else
    path = mainPath;
  int id = 1000 + 100 * panelIndex;
  Panels[panelIndex].Create(_window, _window, 
      id, path, &m_PanelCallbackImp[panelIndex], &AppState);
  PanelsCreated[panelIndex] = true;
}

static void CreateToolbar(
    HWND parent,
    NWindows::NControl::CImageList &imageList,
    NWindows::NControl::CToolBar &toolBar,
    bool LargeButtons)
{
  toolBar.Attach(::CreateWindowEx(0, 
      TOOLBARCLASSNAME,
      NULL, 0
      | WS_VISIBLE
      | TBSTYLE_FLAT
      | TBSTYLE_TOOLTIPS 
      | WS_CHILD
      | CCS_NOPARENTALIGN
      | CCS_NORESIZE 
      | CCS_NODIVIDER
      // | TBSTYLE_AUTOSIZE
      // | CCS_ADJUSTABLE 
      ,0,0,0,0, parent, NULL, g_hInstance, NULL));

  // TB_BUTTONSTRUCTSIZE message, which is required for 
  // backward compatibility.
  toolBar.ButtonStructSize();

  imageList.Create(
      LargeButtons ? 48: 24, 
      LargeButtons ? 36: 24, 
      ILC_MASK, 0, 0);
  toolBar.SetImageList(0, imageList);
}

struct CButtonInfo
{
  UINT commandID;
  UINT BitmapResID;
  UINT Bitmap2ResID;
  UINT StringResID; 
  UINT32 LangID;
  CSysString GetText()const
    { return LangLoadString(StringResID, LangID); };
};

static CButtonInfo g_StandardButtons[] = 
{
  { IDM_COPY_TO, IDB_COPY, IDB_COPY2, IDS_BUTTON_COPY, 0x03020420},
  { IDM_MOVE_TO, IDB_MOVE, IDB_MOVE2, IDS_BUTTON_MOVE, 0x03020421},
  { IDM_DELETE, IDB_DELETE, IDB_DELETE2, IDS_BUTTON_DELETE, 0x03020422} ,
  { IDM_FILE_PROPERTIES, IDB_INFO, IDB_INFO2, IDS_BUTTON_INFO, 0x03020423} 
};

static CButtonInfo g_ArchiveButtons[] = 
{
  { kAddCommand, IDB_ADD, IDB_ADD2, IDS_ADD, 0x03020400},
  { kExtractCommand, IDB_EXTRACT, IDB_EXTRACT2, IDS_EXTRACT, 0x03020401},
  { kTestCommand , IDB_TEST, IDB_TEST2, IDS_TEST, 0x03020402}
};

bool SetButtonText(UINT32 commandID, CButtonInfo *buttons, int numButtons, CSysString &s)
{
  for (int i = 0; i < numButtons; i++)
  {
    const CButtonInfo &b = buttons[i];
    if (b.commandID == commandID)
    {
      s = b.GetText();
      return  true;
    }
  }
  return false;
}

void SetButtonText(UINT32 commandID, CSysString &s)
{
  if (SetButtonText(commandID, g_StandardButtons, sizeof(g_StandardButtons) / 
      sizeof(g_StandardButtons[0]), s))
    return;
  SetButtonText(commandID, g_ArchiveButtons, sizeof(g_StandardButtons) / 
      sizeof(g_ArchiveButtons[0]), s);
}

static void AddButton(
    NControl::CImageList &imageList,
    NControl::CToolBar &toolBar, 
    CButtonInfo &butInfo,
    bool showText,
    bool large)
{
  TBBUTTON but; 
  but.iBitmap = 0; 
  but.idCommand = butInfo.commandID; 
  but.fsState = TBSTATE_ENABLED; 
  but.fsStyle = BTNS_BUTTON
    // | BTNS_AUTOSIZE 
    ;
  but.dwData = 0;

  CSysString s = butInfo.GetText();
  but.iString = 0;
  if (showText)
    but.iString = (INT_PTR )(LPCTSTR)s; 

  but.iBitmap = imageList.GetImageCount();
  HBITMAP b = ::LoadBitmap(g_hInstance, 
    large ? 
    (LPCTSTR)butInfo.BitmapResID:
    (LPCTSTR)butInfo.Bitmap2ResID);
  if (b != 0)
  {
    imageList.AddMasked(b, RGB(255, 0, 255));
    ::DeleteObject(b);
  }
  toolBar.AddButton(1, &but);
}

static void AddBand(NControl::CReBar &reBar, NControl::CToolBar &toolBar)
{
  SIZE size;
  toolBar.GetMaxSize(&size);

  RECT rect;
  toolBar.GetWindowRect(&rect);
  
  REBARBANDINFO rbBand;
  rbBand.cbSize = sizeof(REBARBANDINFO);  // Required
  rbBand.fMask  = RBBIM_STYLE 
    | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_SIZE;
  rbBand.fStyle = RBBS_CHILDEDGE; // RBBS_NOGRIPPER;
  rbBand.cxMinChild = size.cx; // rect.right - rect.left;
  rbBand.cyMinChild = size.cy; // rect.bottom - rect.top;
  rbBand.cyChild = rbBand.cyMinChild;
  rbBand.cx = rbBand.cxMinChild;
  rbBand.cxIdeal = rbBand.cxMinChild;
  rbBand.hwndChild = toolBar;
  reBar.InsertBand(-1, &rbBand);
}

void CApp::ReloadToolbars()
{ 
  if (!_rebar)
    return;
  HWND parent = _rebar;

  while(_rebar.GetBandCount() > 0)
    _rebar.DeleteBand(0);

  _archiveToolBar.Destroy();
  _archiveButtonsImageList.Destroy();

  _standardButtonsImageList.Destroy();
  _standardToolBar.Destroy();

  if (ShowArchiveToolbar)
  {
    CreateToolbar(parent, _archiveButtonsImageList, _archiveToolBar, LargeButtons);
    for (int i = 0; i < sizeof(g_ArchiveButtons) / sizeof(g_ArchiveButtons[0]); i++)
      AddButton(_archiveButtonsImageList, _archiveToolBar, g_ArchiveButtons[i], 
          ShowButtonsLables, LargeButtons);
    AddBand(_rebar, _archiveToolBar);
  }

  if (ShowStandardToolbar)
  {
    CreateToolbar(parent, _standardButtonsImageList, _standardToolBar, LargeButtons);
    for (int i = 0; i < sizeof(g_StandardButtons) / sizeof(g_StandardButtons[0]); i++)
      AddButton(_standardButtonsImageList, _standardToolBar, g_StandardButtons[i], 
          ShowButtonsLables, LargeButtons);
    AddBand(_rebar, _standardToolBar);
  }
}

void CApp::ReloadRebar(HWND hwnd)
{
  _rebar.Destroy();
  if (!ShowArchiveToolbar && !ShowStandardToolbar)
    return;
  if (g_ComCtl32Version >= MAKELONG(71, 4))
  {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC  = ICC_COOL_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);
    
    _rebar.Attach(::CreateWindowEx(WS_EX_TOOLWINDOW,
      REBARCLASSNAME,
      NULL, 
      WS_VISIBLE 
      | WS_BORDER 
      | WS_CHILD 
      | WS_CLIPCHILDREN 
      | WS_CLIPSIBLINGS 
      // | CCS_NODIVIDER  
      // | CCS_NOPARENTALIGN  // it's bead for moveing of two bands
      // | CCS_TOP
      | RBS_VARHEIGHT 
      | RBS_BANDBORDERS
      // | RBS_AUTOSIZE
      ,0,0,0,0, hwnd, NULL, g_hInstance, NULL));
  }
  if (_rebar == 0)
    return;
  REBARINFO rbi;
  rbi.cbSize = sizeof(REBARINFO);  // Required when using this struct.
  rbi.fMask = 0;
  rbi.himl = (HIMAGELIST)NULL;
  _rebar.SetBarInfo(&rbi);
  ReloadToolbars();
}

void CApp::Create(HWND hwnd, const UString &mainPath, int xSizes[2])
{
  ReadToolbar();
  ReloadRebar(hwnd);

  for (int i = 0; i < kNumPanelsMax; i++)
    PanelsCreated[i] = false;

  _window.Attach(hwnd);
  AppState.Read();
  SetListSettings();
  SetShowSystemMenu();
  UString mainPathSpec = mainPath;
  if (LastFocusedPanel >= kNumPanelsMax)
    LastFocusedPanel = 0;

  CListMode listMode;
  ReadListMode(listMode);
  for (i = 0; i < kNumPanelsMax; i++)
  {
    Panels[i]._ListViewMode = listMode.Panels[i];
    Panels[i]._xSize = xSizes[i];
  }
  for (i = 0; i < kNumPanelsMax; i++)
    if (NumPanels > 1 || i == LastFocusedPanel)
    {
      if (NumPanels == 1)
        Panels[i]._xSize = xSizes[0] + xSizes[1];
      CreateOnePanel(i, (i == LastFocusedPanel) ? mainPath : L"");
    }
  Panels[LastFocusedPanel].SetFocusToList();
}

extern void MoveSubWindows(HWND hWnd);

void CApp::SwitchOnOffOnePanel()
{
  if (NumPanels == 1)
  {
    NumPanels++;
    CreateOnePanel(1 - LastFocusedPanel, UString());
    Panels[1 - LastFocusedPanel].Enable(true);
    Panels[1 - LastFocusedPanel].Show(SW_SHOWNORMAL);
  }
  else
  {
    NumPanels--;
    Panels[1 - LastFocusedPanel].Enable(false);
    Panels[1 - LastFocusedPanel].Show(SW_HIDE);
  }
  MoveSubWindows(_window);
}

void CApp::Save()
{
  AppState.Save();
  CListMode listMode;
  for (int i = 0; i < kNumPanelsMax; i++)
  {
    const CPanel &panel = Panels[i];
    UString path;
    if (panel._parentFolders.IsEmpty())
      path = panel._currentFolderPrefix;
    else
      path = GetFolderPath(panel._parentFolders[0].ParentFolder);
    SavePanelPath(i, GetSystemString(path));
    listMode.Panels[i] = panel.GetListViewMode();
  }
  SaveListMode(listMode);
}

void CApp::Release()
{
  // It's for unloading COM dll's: don't change it. 
  for (int i = 0; i < kNumPanelsMax; i++)
    Panels[i].Release();
}

class CWindowDisable
{
  bool _wasEnabled;
  CWindow _window;
public:
  CWindowDisable(HWND window): _window(window) 
  { 
    _wasEnabled = _window.IsEnabled();
    if (_wasEnabled)
      _window.Enable(false); 
  }
  ~CWindowDisable() 
  { 
    if (_wasEnabled)
      _window.Enable(true); 
  }
};

struct CThreadExtract
{
  bool Move;
  CMyComPtr<IFolderOperations> FolderOperations;
  CRecordVector<UINT32> Indices;
  UString DestPath;
  CExtractCallbackImp *ExtractCallbackSpec;
  CMyComPtr<IFolderOperationsExtractCallback> ExtractCallback;
  HRESULT Result;
  
  DWORD Extract()
  {
    NCOM::CComInitializer comInitializer;
    ExtractCallbackSpec->ProgressDialog.WaitCreating();
    if (Move)
    {
      Result = FolderOperations->MoveTo(
          &Indices.Front(), Indices.Size(), 
          DestPath, ExtractCallback);
      // ExtractCallbackSpec->DestroyWindows();
    }
    else
    {
      Result = FolderOperations->CopyTo(
          &Indices.Front(), Indices.Size(), 
          DestPath, ExtractCallback);
      // ExtractCallbackSpec->DestroyWindows();
    }
    ExtractCallbackSpec->ProgressDialog.MyClose();
    return 0;
  }

  static DWORD WINAPI MyThreadFunction(void *param)
  {
    return ((CThreadExtract *)param)->Extract();
  }
};

struct CThreadUpdate
{
  bool Move;
  CMyComPtr<IFolderOperations> FolderOperations;
  UString SrcFolderPrefix;
  UStringVector FileNames;
  CRecordVector<const wchar_t *> FileNamePointers;
  CMyComPtr<IFolderArchiveUpdateCallback> UpdateCallback;
  CUpdateCallback100Imp *UpdateCallbackSpec;
  HRESULT Result;
  
  DWORD Process()
  {
    NCOM::CComInitializer comInitializer;
    UpdateCallbackSpec->ProgressDialog.WaitCreating();
    if (Move)
    {
      {
        throw 1;
        // srcPanel.MessageBoxMyError(L"Move is not supported");
        return 0;
      }
    }
    else
    {
      Result = FolderOperations->CopyFrom(
          SrcFolderPrefix,
          &FileNamePointers.Front(),
          FileNamePointers.Size(),
          UpdateCallback);
    }
    UpdateCallbackSpec->ProgressDialog.MyClose();
    return 0;
  }

  static DWORD WINAPI MyThreadFunction(void *param)
  {
    return ((CThreadUpdate *)param)->Process();
  }
};



void CApp::OnCopy(UStringVector &externalNames, bool move, bool copyToSame, int srcPanelIndex)
{
  bool external = (externalNames.Size() > 0);
  if (external)
    copyToSame = true;

  int destPanelIndex = (NumPanels <= 1) ? srcPanelIndex : (1 - srcPanelIndex);
  CPanel &srcPanel = Panels[srcPanelIndex];
  CPanel &destPanel = Panels[destPanelIndex];
  bool useSrcPanel = true;
  if (!external)
    if (NumPanels != 1)
    {
      if (!srcPanel.IsFSFolder() && !destPanel.IsFSFolder())
      {
        srcPanel.MessageBox(LangLoadStringW(IDS_CANNOT_COPY, 0x03020207));
        return;
      }
      useSrcPanel = copyToSame || destPanel.IsFSFolder();
      if (move && !useSrcPanel)
      {
        srcPanel.MessageBoxMyError(L"Move is not supported");
        return;
      }
    }

  CPanel &panel = useSrcPanel ? srcPanel : destPanel;

  CMyComPtr<IFolderOperations> folderOperations;

  // if (move)
    if (panel._folder.QueryInterface(IID_IFolderOperations, 
        &folderOperations) != S_OK)
    {
      panel.MessageBox(LangLoadStringW(IDS_OPERATION_IS_NOT_SUPPORTED, 0x03020208));
      return;
    }

  CRecordVector<UINT32> indices;
  UString destPath;

  if (external)
  {
    UString message = L"Are you sure you want to copy files to archive \'";
    message += srcPanel._currentFolderPrefix;
    message += L"\'?";
    int res = MessageBoxW(_window, message, L"Confirm File Copy", 
        MB_YESNOCANCEL | MB_ICONQUESTION | MB_TASKMODAL);
    if (res != IDYES)
      return;
  }
  else
  {
  CCopyDialog copyDialog;

  UStringVector copyFolders;
  ReadCopyHistory(copyFolders);

  int i;
  for (i = 0; i < copyFolders.Size(); i++)
    copyDialog.Strings.Add(GetSystemString(copyFolders[i]));

  if (copyToSame)
  {
    int focusedItem = srcPanel._listView.GetFocusedItem();
    if (focusedItem < 0)
      return;
    int realIndex = srcPanel.GetRealItemIndex(focusedItem);
    if (realIndex == -1)
      return;
    indices.Add(realIndex);
    copyDialog.Value = srcPanel.GetItemName(realIndex);
  }
  else
  {
    srcPanel.GetOperatedItemIndices(indices);
    if (indices.Size() == 0)
      return;
    UString destPath = destPanel._currentFolderPrefix;
    if (NumPanels == 1)
    {
      while(!destPath.IsEmpty())
      {
        CFileInfoW fileInfo;
        if (FindFile(destPath, fileInfo))
        {
          if (fileInfo.IsDirectory())
          {
            destPath += L'\\';
            break;
          }
        }
        int pos = destPath.ReverseFind('\\');
        if (pos < 0)
          destPath.Empty();
        else
        {
          destPath = destPath.Left(pos + 1);
            if (destPath.Length() == 3 && destPath[1] == L':')
              break;
          destPath = destPath.Left(pos);
        }
      }
    }
    copyDialog.Value = destPath;
  }
  copyDialog.Title = move ? 
    LangLoadStringW(IDS_MOVE, 0x03020202):
    LangLoadStringW(IDS_COPY, 0x03020201);
  copyDialog.Static = move ? 
    LangLoadStringW(IDS_MOVE_TO, 0x03020204):
    LangLoadStringW(IDS_COPY_TO, 0x03020203);
  if (copyDialog.Create(srcPanel.GetParent()) == IDCANCEL)
    return;

  AddUniqueStringToHeadOfList(copyFolders, GetUnicodeString(
      copyDialog.Value));
  while (copyFolders.Size() > 20)
    copyFolders.DeleteBack();

  SaveCopyHistory(copyFolders);
  
  /// ?????
  SetCurrentDirectory(GetSystemString(srcPanel._currentFolderPrefix));

  if (!NDirectory::MyGetFullPathName(copyDialog.Value, destPath))
  {
    srcPanel.MessageBoxLastError();
    return;
  }

  if (destPath.Length() > 0 && destPath.ReverseFind('\\') == destPath.Length() - 1)
    NDirectory::CreateComplexDirectory(destPath);
  }

  UString title = move ? 
      LangLoadStringW(IDS_MOVING, 0x03020206):
      LangLoadStringW(IDS_COPYING, 0x03020205);
  UString progressWindowTitle = LangLoadStringW(IDS_APP_TITLE, 0x03000000);

  CPanel::CDisableTimerProcessing disableTimerProcessing1(destPanel);
  CPanel::CDisableTimerProcessing disableTimerProcessing2(srcPanel);

  HRESULT result;
  if (useSrcPanel && !external)
  {
    CThreadExtract extracter;
    extracter.ExtractCallbackSpec = new CExtractCallbackImp;
    extracter.ExtractCallback = extracter.ExtractCallbackSpec;
    extracter.ExtractCallbackSpec->ParentWindow = _window;

    extracter.ExtractCallbackSpec->ProgressDialog.MainWindow = _window;
    extracter.ExtractCallbackSpec->ProgressDialog.MainTitle = progressWindowTitle;
    extracter.ExtractCallbackSpec->ProgressDialog.MainAddTitle = title + L" ";
    extracter.ExtractCallbackSpec->OverwriteMode = NExtract::NOverwriteMode::kAskBefore;

    extracter.ExtractCallbackSpec->Init();
    extracter.Move = move;
    extracter.FolderOperations = folderOperations;
    extracter.Indices = indices;;
    extracter.DestPath = GetUnicodeString(destPath);
    CThread thread;
    if (!thread.Create(CThreadExtract::MyThreadFunction, &extracter))
      throw 271824;
    extracter.ExtractCallbackSpec->StartProgressDialog(title);
    result = extracter.Result;
  }
  else
  {
    CThreadUpdate updater;
    updater.UpdateCallbackSpec = new CUpdateCallback100Imp;
    updater.UpdateCallback = updater.UpdateCallbackSpec;
    
    updater.UpdateCallbackSpec->ProgressDialog.MainWindow = _window;
    updater.UpdateCallbackSpec->ProgressDialog.MainTitle = progressWindowTitle;
    updater.UpdateCallbackSpec->ProgressDialog.MainAddTitle = title + UString(L" ");

    updater.UpdateCallbackSpec->Init(_window, false, L"");
    updater.Move = move;
    updater.FolderOperations = folderOperations;
    if (external)
    {
      updater.FileNames.Reserve(externalNames.Size());
      for(int i = 0; i < externalNames.Size(); i++)
        updater.FileNames.Add(externalNames[i]);
    }
    else
    {
      updater.SrcFolderPrefix = srcPanel._currentFolderPrefix;
      updater.FileNames.Reserve(indices.Size());
      for(int i = 0; i < indices.Size(); i++)
        updater.FileNames.Add(srcPanel.GetItemName(indices[i]));
    }
    updater.FileNamePointers.Reserve(updater.FileNames.Size());
    int i;
    for(i = 0; i < updater.FileNames.Size(); i++)
      updater.FileNamePointers.Add(updater.FileNames[i]);
    CThread thread;
    if (!thread.Create(CThreadUpdate::MyThreadFunction, &updater))
      throw 271824;
    updater.UpdateCallbackSpec->StartProgressDialog(title);
    result = updater.Result;
  }

  /*
  if (useSrcPanel)
    extractCallbackSpec->DestroyWindows();
  */
  
  if (result != S_OK)
  {
    disableTimerProcessing1.Restore();
    disableTimerProcessing2.Restore();
    // For Password:
    srcPanel.SetFocusToList();
    if (result != E_ABORT)
      srcPanel.MessageBoxError(result, L"Error");
    return;
  }
  if (copyToSame || move)
  {
    srcPanel.RefreshListCtrlSaveFocused();
  }
  if (!copyToSame)
  {
    destPanel.RefreshListCtrlSaveFocused();
    srcPanel.KillSelection();
  }
  disableTimerProcessing1.Restore();
  disableTimerProcessing2.Restore();
  srcPanel.SetFocusToList();
}


void CApp::OnSetSameFolder(int srcPanelIndex)
{
  if (NumPanels <= 1)
    return;
  const CPanel &srcPanel = Panels[srcPanelIndex];
  CPanel &destPanel = Panels[1 - srcPanelIndex];
  destPanel.BindToPathAndRefresh(srcPanel._currentFolderPrefix);
}

void CApp::OnSetSubFolder(int srcPanelIndex)
{
  if (NumPanels <= 1)
    return;
  const CPanel &srcPanel = Panels[srcPanelIndex];
  CPanel &destPanel = Panels[1 - srcPanelIndex];

  int focusedItem = srcPanel._listView.GetFocusedItem();
  if (focusedItem < 0)
    return;
  int realIndex = srcPanel.GetRealItemIndex(focusedItem);
  if (!srcPanel.IsItemFolder(realIndex))
    return;


  /*
  UString string = srcPanel._currentFolderPrefix + 
      srcPanel.GetItemName(realIndex) + L'\\';
  destPanel.BindToFolder(string);
  */
  CMyComPtr<IFolderFolder> newFolder;
  if (realIndex == -1)
  {
    if (srcPanel._folder->BindToParentFolder(&newFolder) != S_OK)
      return;
  }
  else
  {
    if (srcPanel._folder->BindToFolder(realIndex, &newFolder) != S_OK)
      return;
  }
  destPanel._folder = newFolder;
  destPanel.RefreshListCtrl();
}

/*
int CApp::GetFocusedPanelIndex() const
{
  return LastFocusedPanel;
  HWND hwnd = ::GetFocus();
  while(true)
  {
    if (hwnd == 0)
      return 0;
    for (int i = 0; i < kNumPanelsMax; i++)
    {
      if (PanelsCreated[i] && 
          ((HWND)Panels[i] == hwnd || Panels[i]._listView == hwnd))
        return i;
    }
    hwnd = GetParent(hwnd);
  }
}
  */

static CSysString g_ToolTipBuffer;

void CApp::OnNotify(int ctrlID, LPNMHDR pnmh)
{
  if (pnmh->hwndFrom == _rebar)
  {
    switch(pnmh->code)
    {
      case RBN_HEIGHTCHANGE:
      {
        MoveSubWindows(g_HWND);
        return;
      }
    }
    return ;
  }
  else 
  {
    if (pnmh->code == TTN_GETDISPINFO)
    {
      LPNMTTDISPINFO info = (LPNMTTDISPINFO)pnmh;
      info->hinst = 0;
      g_ToolTipBuffer.Empty();
      SetButtonText(info->hdr.idFrom, g_ToolTipBuffer);
      info->lpszText = (LPTSTR)(LPCTSTR)g_ToolTipBuffer;
      return;
    }
  }
}
