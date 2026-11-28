; Roomservice installer for Windows (Inno Setup 6). The version comes from the build: ISCC /DAppVer=1.0.1 /DFileVer=1.0.1
#ifndef AppVer
  #define AppVer "1.3.4"
#endif
#ifndef FileVer
  #define FileVer "1.3.4"
#endif

[Setup]
AppId={{A44B0D5E-51C0-4B7A-9E0D-5A11C0DE0100}
AppName=Ahh
AppVersion={#AppVer}
AppPublisher=dmajorsound
AppPublisherURL=https://dmajorsound.com
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DirExistsWarning=no
DisableProgramGroupPage=yes
PrivilegesRequired=admin
OutputBaseFilename=Ahh_{#FileVer}_installer_win64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Uninstallable=yes
UninstallDisplayName=Ahh
UninstallFilesDir={commonappdata}\dmajorsound\Ahh
WizardStyle=modern
WizardImageFile=wizard-100.bmp,wizard-200.bmp
WizardSmallImageFile=small-100.bmp,small-200.bmp
WizardImageStretch=no
DisableWelcomePage=no

[Files]
Source: "welcome-icon-100.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "welcome-icon-200.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "welcome-brand-100.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "welcome-brand-200.bmp"; DestDir: "{tmp}"; Flags: dontcopy
Source: "..\..\build\Roomservice_artefacts\Release\VST3\Ahh.vst3\*"; DestDir: "{commoncf64}\VST3\Ahh.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\build\Roomservice_artefacts\Release\CLAP\Ahh.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion

; Ahh was called Roomservice up to 1.0.4 and is the same plug-in (same ids): the old files go, or hosts would find it twice
[InstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Roomservice.vst3"
Type: files; Name: "{commoncf64}\CLAP\Roomservice.clap"
Type: filesandordirs; Name: "{commoncf64}\VST3\RoomservicePro.vst3"
Type: files; Name: "{commoncf64}\CLAP\RoomservicePro.clap"

[Types]
Name: "full"; Description: "VST3 and CLAP"
Name: "custom"; Description: "Choose formats"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 (Nuendo, REAPER and most other DAWs)"; Types: full custom
Name: "clap"; Description: "CLAP (REAPER, Bitwig Studio and other DAWs that support CLAP)"; Types: full custom

[Messages]
WelcomeLabel1=Ahh!%nRoom Simulation
WelcomeLabel2=This installs Ahh as VST3 and CLAP plug-in into the common plug-in folders.%n%nAfterwards, rescan your plug-ins in your DAW.

[Code]
#include "branding.iss"

procedure InitializeWizard();
begin
  BrandWelcome('Ahh! - Room Simulation',
    'This installs Ahh as VST3 and CLAP plug-in into the common plug-in folders.' + #13#10 + #13#10 +
    'Quit your DAW first. Afterwards, rescan your plug-ins in your DAW.');
end;
