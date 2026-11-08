; Installeur Windows de Multidestination Studio (plugin OBS « multidestination-studio »).
; Compilé par GitHub Actions avec Inno Setup :
;   ISCC /DMyVersion=x.y.z /DSrcDir=<dossier du plugin> /DOutDir=<sortie> installer-windows.iss
; <dossier du plugin> contient bin\64bit\multidestination-studio.dll et data\locale\*.ini.

#ifndef MyVersion
  #define MyVersion "0.0.0"
#endif
#ifndef SrcDir
  #define SrcDir "..\release\RelWithDebInfo\multidestination-studio"
#endif
#ifndef OutDir
  #define OutDir "..\release"
#endif

[Setup]
AppId={{6B8C2F4E-1D3A-4E7B-9A51-2C7D8E0F4A13}
AppName=Multidestination Studio for OBS
AppVersion={#MyVersion}
AppPublisher=sebts
AppPublisherURL=https://github.com/sebts-574/multidestination-studio
; Emplacement officiel des plugins OBS (28+), conservé lors des mises à jour d'OBS.
DefaultDirName={commonappdata}\obs-studio\plugins\multidestination-studio
DisableDirPage=yes
; Toujours le dossier ci-dessus : sinon Inno reprend le dossier de
; l'installation précédente (même AppId), c'est-à-dire l'ancien dossier
; « destinations-for-obs », et OBS ne charge pas un plugin dont le dossier
; ne porte pas son nom.
UsePreviousAppDir=no
DisableProgramGroupPage=yes
DisableReadyPage=no
OutputDir={#OutDir}
OutputBaseFilename=multidestination-studio-{#MyVersion}-windows-x64-installer
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=Multidestination Studio for OBS
UninstallFilesDir={app}\uninstall
WizardStyle=modern

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
fr.CloseObs=Fermez OBS Studio avant de continuer.
en.CloseObs=Please close OBS Studio before continuing.

[Messages]
fr.WelcomeLabel2=Cet assistant installe [name/ver] dans OBS Studio.%n%nFermez OBS Studio avant de continuer.
en.WelcomeLabel2=This will install [name/ver] into OBS Studio.%n%nPlease close OBS Studio before continuing.

[InstallDelete]
; Ancien nom du plugin (« destinations-for-obs », versions de test) : sinon
; OBS chargerait les deux.
Type: filesandordirs; Name: "{commonappdata}\obs-studio\plugins\destinations-for-obs"
Type: files; Name: "{commonpf64}\obs-studio\obs-plugins\64bit\destinations-for-obs.dll"
Type: files; Name: "{commonpf64}\obs-studio\obs-plugins\64bit\destinations-for-obs.pdb"
Type: filesandordirs; Name: "{commonpf64}\obs-studio\obs-plugins\64bit\destinations-for-obs"
Type: filesandordirs; Name: "{commonpf64}\obs-studio\data\obs-plugins\destinations-for-obs"
; Version précédente au même endroit.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"
; Copies manuelles mal placées des versions de test.
Type: files; Name: "{commonpf64}\obs-studio\obs-plugins\64bit\multidestination-studio.dll"
Type: files; Name: "{commonpf64}\obs-studio\obs-plugins\64bit\multidestination-studio.pdb"
Type: filesandordirs; Name: "{commonpf64}\obs-studio\obs-plugins\64bit\multidestination-studio"
Type: filesandordirs; Name: "{commonpf64}\obs-studio\data\obs-plugins\multidestination-studio"

[Files]
Source: "{#SrcDir}\*"; DestDir: "{app}"; Excludes: "*.pdb"; Flags: recursesubdirs createallsubdirs ignoreversion

[Code]
function IsObsRunning(): Boolean;
var
  ResultCode: Integer;
begin
  { tasklist renvoie 0 dans tous les cas : on cherche le processus via find. }
  Result := Exec(ExpandConstant('{cmd}'), '/C tasklist /FI "IMAGENAME eq obs64.exe" | find /I "obs64.exe" >NUL',
                 '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  while IsObsRunning() do
  begin
    if MsgBox(CustomMessage('CloseObs'), mbInformation, MB_OKCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;
