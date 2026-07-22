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
Name: "contextmenu"; Description: "Add 'Open kitty here' to the Explorer right-click menu"
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "..\..\dist\kitty\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\kitty"; Filename: "{app}\kitty\launcher\kitty.exe"; WorkingDir: "{%USERPROFILE}"
Name: "{autodesktop}\kitty"; Filename: "{app}\kitty\launcher\kitty.exe"; WorkingDir: "{%USERPROFILE}"; Tasks: desktopicon

[Registry]
; Right-click on a folder
Root: HKLM; Subkey: "Software\Classes\Directory\shell\kitty"; ValueType: string; ValueName: ""; ValueData: "Open kitty here"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Directory\shell\kitty"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\kitty\launcher\kitty.exe"; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Directory\shell\kitty\command"; ValueType: string; ValueName: ""; ValueData: """{app}\kitty\launcher\kitty.exe"" --directory ""%V"""; Tasks: contextmenu
; Right-click on the background of an open folder
Root: HKLM; Subkey: "Software\Classes\Directory\Background\shell\kitty"; ValueType: string; ValueName: ""; ValueData: "Open kitty here"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Directory\Background\shell\kitty"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\kitty\launcher\kitty.exe"; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Directory\Background\shell\kitty\command"; ValueType: string; ValueName: ""; ValueData: """{app}\kitty\launcher\kitty.exe"" --directory ""%V"""; Tasks: contextmenu
; Right-click on a drive
Root: HKLM; Subkey: "Software\Classes\Drive\shell\kitty"; ValueType: string; ValueName: ""; ValueData: "Open kitty here"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Drive\shell\kitty"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\kitty\launcher\kitty.exe"; Tasks: contextmenu
Root: HKLM; Subkey: "Software\Classes\Drive\shell\kitty\command"; ValueType: string; ValueName: ""; ValueData: """{app}\kitty\launcher\kitty.exe"" --directory ""%V"""; Tasks: contextmenu

[Code]
const EnvKey = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';

function BinDir(): string;
begin
  Result := ExpandConstant('{app}') + '\kitty\launcher';
end;

procedure EnvAddPath();
var
  Paths: string;
begin
  if not RegQueryStringValue(HKLM, EnvKey, 'Path', Paths) then
    Paths := '';
  if Pos(';' + Uppercase(BinDir()) + ';', ';' + Uppercase(Paths) + ';') > 0 then
    exit;
  if (Paths <> '') and (Paths[Length(Paths)] <> ';') then
    Paths := Paths + ';';
  Paths := Paths + BinDir();
  RegWriteExpandStringValue(HKLM, EnvKey, 'Path', Paths);
end;

procedure EnvRemovePath();
var
  Paths, Upper: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKLM, EnvKey, 'Path', Paths) then
    exit;
  Upper := ';' + Uppercase(Paths) + ';';
  P := Pos(';' + Uppercase(BinDir()) + ';', Upper);
  if P = 0 then
    exit;
  { P is an index into the ';'-prefixed string: cut the entry and one ';' }
  Delete(Paths, P, Length(BinDir()) + 1);
  RegWriteExpandStringValue(HKLM, EnvKey, 'Path', Paths);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    EnvAddPath();
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    EnvRemovePath();
end;
