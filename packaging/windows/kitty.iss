; Inno Setup script for the kitty Windows port.
; Build: run make-dist.sh first (creates ..\..\dist\kitty), then
;   ISCC.exe /DAppVersion=<version> kitty.iss
; Produces dist\kitty-setup.exe. Per-user install: no admin required.

#ifndef AppVersion
  #define AppVersion "0.48.0"
#endif

[Setup]
AppId={{A303A4CC-F4EE-40BE-B881-1C25CDB1AC19}
AppName=kitty
AppVersion={#AppVersion}
AppPublisher=Kovid Goyal (Windows port by ecstra)
AppPublisherURL=https://sw.kovidgoyal.net/kitty/
DefaultDirName={autopf}\kitty
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\dist
OutputBaseFilename=kitty-setup
SetupIconFile=..\..\kitty\launcher\kitty.ico
UninstallDisplayIcon={app}\kitty\launcher\kitty.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ChangesEnvironment=yes

[Tasks]
Name: "addtopath"; Description: "Add kitty to your PATH (so 'kitty' and 'kitten' work in any terminal)"
Name: "contextmenu"; Description: "Add 'Open in kitty' to the Explorer right-click menu"
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "..\..\dist\kitty\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion
Source: "..\..\dist\kitty-menu.msix"; DestDir: "{app}\msix"; Tasks: contextmenu; Check: IsWin11
Source: "..\..\dist\kitty-menu.cer"; DestDir: "{app}\msix"; Tasks: contextmenu; Check: IsWin11

[Icons]
Name: "{autoprograms}\kitty"; Filename: "{app}\kitty\launcher\kitty.exe"; WorkingDir: "{%USERPROFILE}"
Name: "{autodesktop}\kitty"; Filename: "{app}\kitty\launcher\kitty.exe"; WorkingDir: "{%USERPROFILE}"; Tasks: desktopicon

[Registry]
; Classic registry verbs. These are the whole entry on Windows 10, whose menu
; reads nothing else. On Windows 11 the sparse MSIX package supplies the modern
; menu, and these cover Show more options, which the modern menu falls back to.
; If a Windows 11 build ever shows the packaged verb in Show more options as
; well, this would read as a duplicate there and should be gated back behind
; "Check: not IsWin11".
; Right-click on a folder
Root: HKLM; Subkey: "Software\Classes\Directory\shell\kitty"; ValueType: string; ValueName: ""; ValueData: "Open in kitty"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Directory\shell\kitty"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\kitty\launcher\kitty.exe"; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Directory\shell\kitty\command"; ValueType: string; ValueName: ""; ValueData: """{app}\kitty\launcher\kitty.exe"" --directory ""%V"""; Tasks: contextmenu
; Right-click on the background of an open folder
Root: HKLM; Subkey: "Software\Classes\Directory\Background\shell\kitty"; ValueType: string; ValueName: ""; ValueData: "Open in kitty"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Directory\Background\shell\kitty"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\kitty\launcher\kitty.exe"; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Directory\Background\shell\kitty\command"; ValueType: string; ValueName: ""; ValueData: """{app}\kitty\launcher\kitty.exe"" --directory ""%V"""; Tasks: contextmenu
; Right-click on a drive
Root: HKLM; Subkey: "Software\Classes\Drive\shell\kitty"; ValueType: string; ValueName: ""; ValueData: "Open in kitty"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Drive\shell\kitty"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\kitty\launcher\kitty.exe"; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Drive\shell\kitty\command"; ValueType: string; ValueName: ""; ValueData: """{app}\kitty\launcher\kitty.exe"" --directory ""%V"""; Tasks: contextmenu

[Code]
const EnvKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';

function IsWin11(): Boolean;
var
  V: TWindowsVersion;
begin
  GetWindowsVersionEx(V);
  Result := V.Build >= 22000;
end;

function BinDir(): string;
begin
  Result := ExpandConstant('{app}') + '\bin';
end;

procedure EnvAddPath(const Dir: string);
var
  Paths: string;
begin
  if not RegQueryStringValue(HKLM, EnvKey, 'Path', Paths) then
    Paths := '';
  if Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Paths) + ';') > 0 then
    exit;
  if (Paths <> '') and (Paths[Length(Paths)] <> ';') then
    Paths := Paths + ';';
  Paths := Paths + Dir;
  RegWriteExpandStringValue(HKLM, EnvKey, 'Path', Paths);
end;

procedure EnvRemovePath(const Dir: string);
var
  Paths, Upper: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKLM, EnvKey, 'Path', Paths) then
    exit;
  Upper := ';' + Uppercase(Paths) + ';';
  P := Pos(';' + Uppercase(Dir) + ';', Upper);
  if P = 0 then
    exit;
  { P is an index into the ';'-prefixed string: cut the entry and one ';' }
  Delete(Paths, P, Length(Dir) + 1);
  RegWriteExpandStringValue(HKLM, EnvKey, 'Path', Paths);
end;

{ Windows 11 modern context menu: trust the self-signed certificate and
  register the sparse MSIX package pointing at the install directory. }
procedure RegisterMsix();
var
  R: Integer;
begin
  Exec(ExpandConstant('{sys}\certutil.exe'),
       ExpandConstant('-addstore -f Root "{app}\msix\kitty-menu.cer"'),
       '', SW_HIDE, ewWaitUntilTerminated, R);
  Exec(ExpandConstant('{sys}\certutil.exe'),
       ExpandConstant('-addstore -f TrustedPeople "{app}\msix\kitty-menu.cer"'),
       '', SW_HIDE, ewWaitUntilTerminated, R);
  { Remove any existing registration first, so re-installs of the same version
    still pick up a changed package. }
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
       '-NoProfile -ExecutionPolicy Bypass -Command "Get-AppxPackage Ecstra.Kitty.ContextMenu | Remove-AppxPackage"',
       '', SW_HIDE, ewWaitUntilTerminated, R);
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
       ExpandConstant('-NoProfile -ExecutionPolicy Bypass -Command "Add-AppxPackage -Path ''{app}\msix\kitty-menu.msix'' -ExternalLocation ''{app}''"'),
       '', SW_HIDE, ewWaitUntilTerminated, R);
end;

procedure UnregisterMsix();
var
  R: Integer;
begin
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
       '-NoProfile -ExecutionPolicy Bypass -Command "Get-AppxPackage Ecstra.Kitty.ContextMenu | Remove-AppxPackage"',
       '', SW_HIDE, ewWaitUntilTerminated, R);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    if WizardIsTaskSelected('addtopath') then begin
      EnvAddPath(BinDir());
      { Older installs put the DLL-laden launcher directory on PATH: remove it. }
      EnvRemovePath(ExpandConstant('{app}') + '\kitty\launcher');
    end;
    if WizardIsTaskSelected('contextmenu') and IsWin11() then
      RegisterMsix();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    UnregisterMsix();
  if CurUninstallStep = usPostUninstall then begin
    EnvRemovePath(BinDir());
    EnvRemovePath(ExpandConstant('{app}') + '\kitty\launcher');
  end;
end;
