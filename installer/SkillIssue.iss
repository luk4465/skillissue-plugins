; SkillIssue plugins - Windows installer (Inno Setup 6)
; Build:  installer\build-installer.bat   (after build-win.bat)

#define AppVer "1.6.0"
#define Build "..\build"

[Setup]
AppId={{7B1F3C2A-5E4D-4C6B-9A1F-5149534B494C}
AppName=SkillIssue Plugins
AppVersion={#AppVer}
AppPublisher=SkillIssue
AppPublisherURL=https://skillissue.site
DefaultDirName={autopf}\SkillIssue
DisableDirPage=yes
DefaultGroupName=SkillIssue
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=SkillIssue-Plugins-Setup-{#AppVer}
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
WizardImageFile=wizard.bmp
WizardSmallImageFile=wizard-small.bmp
UninstallDisplayName=SkillIssue Plugins
InfoAfterFile=AFTER-INSTALL.txt

[Types]
Name: "full"; Description: "All plugins"
Name: "custom"; Description: "Choose plugins"; Flags: iscustom

[Components]
Name: "riser";  Description: "SI Riser - reverse reverb riser";          Types: full custom
Name: "reverb"; Description: "SI Reverb - algorithmic reverb";           Types: full custom
Name: "eq";     Description: "SI EQ - dynamic equalizer";                Types: full custom
Name: "tune";   Description: "SI Tune - real-time pitch correction";     Types: full custom
Name: "comp";   Description: "SI 76 - FET compressor (Blue / Black)";    Types: full custom
Name: "saturn"; Description: "SI Saturn - multiband distortion";         Types: full custom
Name: "echo";   Description: "SI Echo - tape and analog echo";           Types: full custom
Name: "rack";   Description: "SI Rack - virtual mix rack";               Types: full custom
Name: "pitch";  Description: "SI Pitch - note editor (Melodyne style)";   Types: full custom

[Files]
Source: "{#Build}\SIRiser_artefacts\Release\VST3\SI Riser.vst3\*";   DestDir: "{commoncf64}\VST3\SI Riser.vst3";   Components: riser;  Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Build}\SIReverb_artefacts\Release\VST3\SI Reverb.vst3\*"; DestDir: "{commoncf64}\VST3\SI Reverb.vst3"; Components: reverb; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Build}\SIEQ_artefacts\Release\VST3\SI EQ.vst3\*";         DestDir: "{commoncf64}\VST3\SI EQ.vst3";     Components: eq;     Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Build}\SITune_artefacts\Release\VST3\SI Tune.vst3\*";     DestDir: "{commoncf64}\VST3\SI Tune.vst3";   Components: tune;   Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Build}\SI76_artefacts\Release\VST3\SI 76.vst3\*";         DestDir: "{commoncf64}\VST3\SI 76.vst3";     Components: comp;   Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Build}\SISaturn_artefacts\Release\VST3\SI Saturn.vst3\*"; DestDir: "{commoncf64}\VST3\SI Saturn.vst3"; Components: saturn; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Build}\SIEcho_artefacts\Release\VST3\SI Echo.vst3\*";     DestDir: "{commoncf64}\VST3\SI Echo.vst3";   Components: echo;   Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Build}\SIRack_artefacts\Release\VST3\SI Rack.vst3\*";     DestDir: "{commoncf64}\VST3\SI Rack.vst3";   Components: rack;   Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Build}\SIPitch_artefacts\Release\VST3\SI Pitch.vst3\*";   DestDir: "{commoncf64}\VST3\SI Pitch.vst3";  Components: pitch;  Flags: ignoreversion recursesubdirs createallsubdirs
Source: "AFTER-INSTALL.txt"; DestDir: "{app}"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\SI Riser.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\SI Reverb.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\SI EQ.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\SI Tune.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\SI 76.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\SI Saturn.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\SI Echo.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\SI Rack.vst3"
Type: filesandordirs; Name: "{commoncf64}\VST3\SI Pitch.vst3"
