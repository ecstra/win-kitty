; Inno Setup 6.7 script for the kitty Windows port.
; Build: make-installer.sh, which runs make-dist.sh (creates ..\..\dist\kitty)
; and make-installer-art.ps1 (the pages' art), then
;   ISCC.exe /DAppVersion=<version> kitty.iss
; Produces dist\kitty-setup.exe.
;
; Setup asks whether to install for the current user only, with no admin
; prompt, or for everyone, with one. In the first, {autopf} is the user's own
; programs folder and HKA is HKCU, in the second Program Files and HKLM, so
; every path and registry key below follows the choice. An upgrade keeps the
; choice the earlier install made, UsePreviousPrivileges being on by default.

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
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\dist
OutputBaseFilename=kitty-setup
SetupIconFile=..\..\kitty\launcher\kitty.ico
UninstallDisplayIcon={app}\kitty\launcher\kitty.exe
Compression=lzma2/max
SolidCompression=yes
ChangesEnvironment=yes
; Windows 11's look, light or dark with Windows, on art made from kitty's icon
WizardStyle=modern dynamic windows11 hidebevels
WizardBackImageFile=..\..\dist\installer-art\light-*.png
WizardBackImageFileDynamicDark=..\..\dist\installer-art\dark-*.png
WizardImageFile=
WizardSmallImageFile=

[Tasks]
Name: "addtopath"; Description: "Add kitty to your PATH (so 'kitty' and 'kitten' work in any terminal)"
Name: "contextmenu"; Description: "Add 'Open in kitty' to the Explorer right-click menu"
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "..\..\dist\kitty\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion
Source: "..\..\dist\kitty-menu.msix"; DestDir: "{app}\msix"; Tasks: contextmenu; Check: CanRegisterMsix
Source: "..\..\dist\kitty-menu.cer"; DestDir: "{app}\msix"; Tasks: contextmenu; Check: CanRegisterMsix

[Icons]
; The shortcut keeps %USERPROFILE% as it is, which Windows expands for whoever
; opens it, where {%USERPROFILE} would be the installing admin's folder
Name: "{autoprograms}\kitty"; Filename: "{app}\kitty\launcher\kitty.exe"; WorkingDir: "%USERPROFILE%"
Name: "{autodesktop}\kitty"; Filename: "{app}\kitty\launcher\kitty.exe"; WorkingDir: "%USERPROFILE%"; Tasks: desktopicon

[Registry]
; Run (Win+R) looks here before PATH, so kitty typed there opens the window
; directly rather than through the console shim on PATH, which shows a console
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\kitty.exe"; ValueType: string; ValueName: ""; ValueData: "{app}\kitty\launcher\kitty.exe"; Flags: uninsdeletekey
; Classic registry verbs. These are the whole entry on Windows 10, whose menu
; reads nothing else. On Windows 11 the sparse MSIX package supplies the modern
; menu, and these cover Show more options, which the modern menu falls back to.
; If a Windows 11 build ever shows the packaged verb in Show more options as
; well, this would read as a duplicate there and should be gated back behind
; "Check: not IsWin11".
; Each verb passes "%V." rather than "%V". A drive's root ends in a backslash,
; which the C runtime reads before a closing quote as an escaped quote, and
; Windows drops the trailing dot of a folder's path.
; Right-click on a folder
Root: HKA; Subkey: "Software\Classes\Directory\shell\kitty"; ValueType: string; ValueName: ""; ValueData: "Open in kitty"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\kitty"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\kitty\launcher\kitty.exe"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\kitty\command"; ValueType: string; ValueName: ""; ValueData: """{app}\kitty\launcher\kitty.exe"" --directory ""%V."""; Tasks: contextmenu
; Right-click on the background of an open folder
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\kitty"; ValueType: string; ValueName: ""; ValueData: "Open in kitty"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\kitty"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\kitty\launcher\kitty.exe"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\kitty\command"; ValueType: string; ValueName: ""; ValueData: """{app}\kitty\launcher\kitty.exe"" --directory ""%V."""; Tasks: contextmenu
; Right-click on a drive
Root: HKA; Subkey: "Software\Classes\Drive\shell\kitty"; ValueType: string; ValueName: ""; ValueData: "Open in kitty"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Drive\shell\kitty"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\kitty\launcher\kitty.exe"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Drive\shell\kitty\command"; ValueType: string; ValueName: ""; ValueData: """{app}\kitty\launcher\kitty.exe"" --directory ""%V."""; Tasks: contextmenu

[Code]
const
  MachineEnvKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  UserEnvKey = 'Environment';
  ReparsePoint = $400;  { FILE_ATTRIBUTE_REPARSE_POINT, a junction or a symlink }

{ PATH lives in the machine's environment for everyone, the user's for me
  only. Only Setup can tell which, since in the uninstaller IsAdminInstallMode
  is true whenever it runs elevated, which a for me only install's uninstaller
  also does when admin rights were around. }
function EnvRoot(): Integer;
begin
  if IsAdminInstallMode() then Result := HKLM else Result := HKCU;
end;

function EnvKey(): string;
begin
  if IsAdminInstallMode() then Result := MachineEnvKey else Result := UserEnvKey;
end;

function IsWin11(): Boolean;
var
  V: TWindowsVersion;
begin
  GetWindowsVersionEx(V);
  Result := V.Build >= 22000;
end;

{ Windows takes the modern menu's package only when its self-signed
  certificate is trusted for the whole machine, which needs admin. An install
  for me only gets the classic entries, under Show more options. }
function CanRegisterMsix(): Boolean;
begin
  Result := IsWin11() and IsAdminInstallMode();
end;

function BinDir(): string;
begin
  Result := ExpandConstant('{app}') + '\bin';
end;

{ Adds Dir as the last entry of PATH. A PATH that ended in ';' keeps ending in
  one, so taking Dir out again with the ';' before it gives back what was there. }
procedure EnvAddPath(Root: Integer; const Key, Dir: string);
var
  Paths: string;
begin
  if not RegQueryStringValue(Root, Key, 'Path', Paths) then
    Paths := '';
  if Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Paths) + ';') > 0 then
    exit;
  if Paths = '' then
    Paths := Dir
  else if Paths[Length(Paths)] = ';' then
    Paths := Paths + Dir + ';'
  else
    Paths := Paths + ';' + Dir;
  RegWriteExpandStringValue(Root, Key, 'Path', Paths);
end;

{ Takes out Dir and the ';' before it, so the rest of PATH is left exactly as
  it was, and a PATH that held nothing else goes away, as before the install. }
procedure EnvRemovePath(Root: Integer; const Key, Dir: string);
var
  Paths, Wrapped: string;
  P: Integer;
begin
  if not RegQueryStringValue(Root, Key, 'Path', Paths) then
    exit;
  Wrapped := ';' + Paths + ';';
  P := Pos(';' + Uppercase(Dir) + ';', Uppercase(Wrapped));
  if P = 0 then
    exit;
  Delete(Wrapped, P, Length(Dir) + 1);
  { Drop the ';' added at either end }
  Paths := Copy(Wrapped, 2, Length(Wrapped) - 2);
  if Paths = '' then
    RegDeleteValue(Root, Key, 'Path')
  else
    RegWriteExpandStringValue(Root, Key, 'Path', Paths);
end;

{ The uninstaller cannot tell which PATH the install wrote to, so it takes this
  install's own folders out of both. Another install of kitty lives in another
  folder, so its entry stays. The machine's PATH needs admin, which an
  uninstall of a for everyone install has. }
procedure EnvRemoveEverywhere(const Dir: string);
begin
  EnvRemovePath(HKCU, UserEnvKey, Dir);
  EnvRemovePath(HKLM, MachineEnvKey, Dir);
end;

{ kitty's Python writes its caches into the install folder as it runs, which
  the uninstall would leave behind, and the folder with them. Only kitty's own
  folders are walked, and never through a junction or a symlink, since the
  install folder may be one the user already kept other things in. }
procedure DeletePyCaches(const Dir: string);
var
  F: TFindRec;
begin
  if FindFirst(Dir + '\*', F) then begin
    try
      repeat
        if ((F.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0) and ((F.Attributes and ReparsePoint) = 0) and
            (F.Name <> '.') and (F.Name <> '..') then begin
          if CompareText(F.Name, '__pycache__') = 0 then
            DelTree(Dir + '\' + F.Name, True, True, True)
          else
            DeletePyCaches(Dir + '\' + F.Name);
        end;
      until not FindNext(F);
    finally
      FindClose(F);
    end;
  end;
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

{ Removes the package for every account that has it, since an uninstall by
  another account than the one that installed left its registration pointing
  at a deleted folder, then takes the certificate RegisterMsix added out of the
  machine's trusted stores, matched by its thumbprint, so the machine stops
  trusting a root whose private key ships in kitty's source. }
procedure UnregisterMsix();
var
  R: Integer;
begin
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
       '-NoProfile -ExecutionPolicy Bypass -Command "Get-AppxPackage -AllUsers Ecstra.Kitty.ContextMenu | Remove-AppxPackage -AllUsers"',
       '', SW_HIDE, ewWaitUntilTerminated, R);
  Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
       ExpandConstant('-NoProfile -ExecutionPolicy Bypass -Command "$c = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 ''{app}\msix\kitty-menu.cer''; foreach ($n in ''Root'', ''TrustedPeople'') {{ $s = New-Object System.Security.Cryptography.X509Certificates.X509Store($n, ''LocalMachine''); $s.Open(''ReadWrite''); $s.Remove($c); $s.Close() }"'),
       '', SW_HIDE, ewWaitUntilTerminated, R);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    if WizardIsTaskSelected('addtopath') then begin
      EnvAddPath(EnvRoot(), EnvKey(), BinDir());
      { Older installs put the DLL-laden launcher directory on PATH: remove it. }
      EnvRemovePath(EnvRoot(), EnvKey(), ExpandConstant('{app}') + '\kitty\launcher');
    end;
    if WizardIsTaskSelected('contextmenu') and CanRegisterMsix() then
      RegisterMsix();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App: string;
begin
  App := ExpandConstant('{app}');
  if CurUninstallStep = usUninstall then begin
    { Only an install that registered the menu package, which copied it to
      msix, takes it away, so a for me only copy leaves the menu of a for
      everyone install of the same account alone }
    if FileExists(App + '\msix\kitty-menu.msix') then
      UnregisterMsix();
    DelTree(App + '\__pycache__', True, True, True);
    DeletePyCaches(App + '\kitty');
    DeletePyCaches(App + '\kittens');
    DeletePyCaches(App + '\shell-integration');
    DeletePyCaches(App + '\pylib');
  end;
  if CurUninstallStep = usPostUninstall then begin
    EnvRemoveEverywhere(BinDir());
    EnvRemoveEverywhere(App + '\kitty\launcher');
  end;
end;
