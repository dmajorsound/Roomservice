; Ahh uninstaller for Windows (Inno Setup 6): removes Ahh and Roomservice (its name up to 1.0.4), VST3 and CLAP.
; Projects and presets stay untouched. ISCC /DAppVer=1.0.5 /DFileVer=1.0.5
#ifndef AppVer
  #define AppVer "1.3.3"
#endif
#ifndef FileVer
  #define FileVer "1.3.3"
#endif

[Setup]
AppId=Ahh_Uninstaller
AppName=Ahh Uninstaller
AppVersion={#AppVer}
AppPublisher=dmajorsound
AppPublisherURL=https://dmajorsound.com
CreateAppDir=no
DisableProgramGroupPage=yes
DisableReadyPage=no
PrivilegesRequired=admin
OutputBaseFilename=Ahh_{#FileVer}_uninstaller_win64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Uninstallable=no
CloseApplications=no
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

[Messages]
SetupWindowTitle=Uninstall Ahh!
WelcomeLabel1=Uninstall Ahh!
WelcomeLabel2=This removes Ahh as VST3 and CLAP plug-in from the common plug-in folders - and Roomservice, as the plug-in was called up to version 1.0.4.%n%nYour projects and presets stay untouched.%n%nQuit your DAW first.
WizardReady=Ready to uninstall
ReadyLabel1=Ahh will now be removed.
ReadyLabel2a=Click Uninstall to continue.
ReadyLabel2b=Click Uninstall to continue.
ButtonInstall=&Uninstall
WizardInstalling=Uninstalling
InstallingLabel=Please wait while Ahh is removed.
FinishedHeadingLabel=Ahh is removed
FinishedLabelNoIcons=Ahh has been removed. Rescan your plug-ins in your DAW.
FinishedLabel=Ahh has been removed. Rescan your plug-ins in your DAW.

[Code]
#include "branding.iss"

procedure InitializeWizard();
begin
  BrandWelcome('Uninstall Ahh!',
    'This removes Ahh as VST3 and CLAP plug-in from the common plug-in folders - and Roomservice, as the plug-in was called up to version 1.0.4. Your projects and presets stay untouched.' + #13#10 + #13#10 +
    'Quit your DAW first.');
end;

procedure Gone(Path: String; IsDir: Boolean; var Failed: Boolean);
begin
  if IsDir then begin
    if DirExists(Path) and not DelTree(Path, True, True, True) then Failed := True;
  end else begin
    if FileExists(Path) and not DeleteFile(Path) then Failed := True;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Unins: String;
  Code: Integer;
  Failed: Boolean;
begin
  if CurStep <> ssInstall then exit;
  Failed := False;
  { the uninstaller the installer left behind also clears the Installed apps entry }
  Unins := ExpandConstant('{commonappdata}\dmajorsound\Ahh\unins000.exe');
  if FileExists(Unins) then
    Exec(Unins, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Gone(ExpandConstant('{commoncf64}\VST3\Ahh.vst3'), True, Failed);
  Gone(ExpandConstant('{commoncf64}\VST3\Roomservice.vst3'), True, Failed);
  Gone(ExpandConstant('{commoncf64}\CLAP\Ahh.clap'), False, Failed);
  Gone(ExpandConstant('{commoncf64}\CLAP\Roomservice.clap'), False, Failed);
  Gone(ExpandConstant('{commoncf64}\VST3\RoomservicePro.vst3'), True, Failed);
  Gone(ExpandConstant('{commoncf64}\CLAP\RoomservicePro.clap'), False, Failed);
  RegDeleteKeyIncludingSubkeys(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{6F1C2B7E-4A3D-4E8B-9C21-7D5E0A1B2C33}_is1');
  RegDeleteKeyIncludingSubkeys(HKLM, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{A44B0D5E-51C0-4B7A-9E0D-5A11C0DE0100}_is1');
  DelTree(ExpandConstant('{commonappdata}\dmajorsound\Roomservice'), True, True, True);
  DelTree(ExpandConstant('{commonappdata}\dmajorsound\Ahh'), True, True, True);
  if Failed then
    MsgBox('Ahh could not be removed completely because an audio application is still running.' + #13#10 +
           'Quit it and run this uninstaller again.', mbError, MB_OK);
end;
