; NOIZE installer.  Build with:  makensis /DSRC_DIR=<path to dist\NOIZE> /DVERSION=0.1.0 installer\noize.nsi
!ifndef SRC_DIR
  !error "Pass /DSRC_DIR=<folder produced by cmake --install>"
!endif
!ifndef VERSION
  !define VERSION "0.1.0"
!endif
!include "MUI2.nsh"
Name "NOIZE"
OutFile "NOIZE-${VERSION}-Setup.exe"
InstallDir "$PROGRAMFILES64\NOIZE"
InstallDirRegKey HKLM "Software\NOIZE" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\NOIZE.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "NOIZE" SecMain
  SetOutPath "$INSTDIR"
  File "${SRC_DIR}\NOIZE.exe"
  File "${SRC_DIR}\README.md"
  File "${SRC_DIR}\LICENSE-NOTICE.md"
  SetOutPath "$INSTDIR\docs"
  File /r "${SRC_DIR}\docs\*.*"
  SetOutPath "$INSTDIR\examples"
  File /r "${SRC_DIR}\examples\*.*"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\NOIZE"
  CreateShortCut "$SMPROGRAMS\NOIZE\NOIZE.lnk" "$INSTDIR\NOIZE.exe"
  CreateShortCut "$SMPROGRAMS\NOIZE\Uninstall NOIZE.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$DESKTOP\NOIZE.lnk" "$INSTDIR\NOIZE.exe"
  WriteRegStr HKLM "Software\NOIZE" "InstallDir" "$INSTDIR"
  WriteRegStr HKCR ".noize" "" "NOIZE.Project"
  WriteRegStr HKCR "NOIZE.Project" "" "NOIZE Project"
  WriteRegStr HKCR "NOIZE.Project\shell\open\command" "" '"$INSTDIR\NOIZE.exe" "%1"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NOIZE" "DisplayName" "NOIZE"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NOIZE" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NOIZE" "UninstallString" '"$INSTDIR\Uninstall.exe"'
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\NOIZE.exe"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\LICENSE-NOTICE.md"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR\docs"
  RMDir /r "$INSTDIR\examples"
  RMDir "$INSTDIR"
  Delete "$SMPROGRAMS\NOIZE\*.lnk"
  RMDir "$SMPROGRAMS\NOIZE"
  Delete "$DESKTOP\NOIZE.lnk"
  DeleteRegKey HKCR ".noize"
  DeleteRegKey HKCR "NOIZE.Project"
  DeleteRegKey HKLM "Software\NOIZE"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\NOIZE"
SectionEnd
