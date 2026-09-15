Unicode True
SetCompressor /SOLID lzma

!include "MUI2.nsh"

!define ProductName "LAN Messenger"
!define ProductVersion "1.2.39"
!define ProductRevision "Revision 2026-09-15"
!define AppExec "lmc.exe"
!define Uninstaller "uninstall.exe"
!define UninstKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\LAN Messenger"

!ifndef BuildDir
  !error "BuildDir must point to the packaged application directory"
!endif

!ifndef OutputFile
  !define OutputFile "LAN-Messenger-Searchable-History-Setup.exe"
!endif

Name "${ProductName}"
OutFile "${OutputFile}"
VIProductVersion "1.2.39.0"
VIAddVersionKey /LANG=1033 "ProductName" "MBC LAN Messenger"
VIAddVersionKey /LANG=1033 "FileDescription" "MBC LAN Messenger Installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${ProductVersion} (${ProductRevision})"
VIAddVersionKey /LANG=1033 "ProductVersion" "${ProductVersion} (${ProductRevision})"
InstallDir "$PROGRAMFILES64\${ProductName}"
InstallDirRegKey HKLM "${UninstKey}" "InstallLocation"
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${AppExec}"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "${BuildDir}\*.*"

  WriteUninstaller "$INSTDIR\${Uninstaller}"
  WriteRegStr HKLM "${UninstKey}" "DisplayName" "${ProductName}"
  WriteRegStr HKLM "${UninstKey}" "DisplayVersion" "${ProductVersion} - ${ProductRevision}"
  WriteRegStr HKLM "${UninstKey}" "DisplayIcon" "$INSTDIR\${AppExec},0"
  WriteRegStr HKLM "${UninstKey}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UninstKey}" "UninstallString" '$"$INSTDIR\${Uninstaller}$"'
  WriteRegDWORD HKLM "${UninstKey}" "NoModify" 1
  WriteRegDWORD HKLM "${UninstKey}" "NoRepair" 1

  CreateDirectory "$SMPROGRAMS\${ProductName}"
  CreateShortcut "$SMPROGRAMS\${ProductName}\${ProductName}.lnk" "$INSTDIR\${AppExec}"
  CreateShortcut "$SMPROGRAMS\${ProductName}\Uninstall ${ProductName}.lnk" "$INSTDIR\${Uninstaller}"
  CreateShortcut "$DESKTOP\${ProductName}.lnk" "$INSTDIR\${AppExec}"

  nsExec::ExecToLog 'netsh advfirewall firewall add rule name="${ProductName}" dir=in action=allow program="$INSTDIR\${AppExec}" enable=yes profile=private'
SectionEnd

Section "Uninstall"
  nsExec::ExecToLog 'netsh advfirewall firewall delete rule name="${ProductName}" program="$INSTDIR\${AppExec}"'
  Delete "$DESKTOP\${ProductName}.lnk"
  RMDir /r "$SMPROGRAMS\${ProductName}"
  DeleteRegKey HKLM "${UninstKey}"
  RMDir /r "$INSTDIR"
SectionEnd
