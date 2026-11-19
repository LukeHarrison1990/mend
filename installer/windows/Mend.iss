; SPDX-License-Identifier: MIT
; Windows installer for Mend (Inno Setup 6).
;
; Build from a staged release folder (scripts/package.sh makes one):
;   ISCC.exe /DVersion=0.8.0 /DStage=C:\path\to\Mend-0.8.0-windows installer\windows\Mend.iss
;
; The user picks what to install: the editor, the standalone apps, and
; each plugin format separately. Plugins go to the standard shared folders
; every host searches (Common Files\VST3, \CLAP and \LV2).

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Stage
  #error Pass /DStage=<staged release folder>
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

[Setup]
AppId={{6E8D3C47-2A1B-4E9F-9C55-6B2E0D41A7F3}
AppName=Mend
AppVersion={#Version}
AppVerName=Mend {#Version}
AppPublisher=Mend contributors
AppPublisherURL=https://github.com/LukeHarrison1990/mend
AppSupportURL=https://github.com/LukeHarrison1990/mend/issues
AppUpdatesURL=https://github.com/LukeHarrison1990/mend/releases
DefaultDirName={autopf}\Mend
DefaultGroupName=Mend
DisableProgramGroupPage=yes
LicenseFile={#Stage}\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=Mend-{#Version}-windows-setup
SetupIconFile=..\..\app\resources\mend.ico
WizardImageFile=wizard-large-1.bmp,wizard-large-2.bmp
WizardSmallImageFile=wizard-small-55.bmp,wizard-small-110.bmp
UninstallDisplayIcon={app}\MendEditor.exe
UninstallDisplayName=Mend {#Version}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; plugins go to Program Files\Common Files, which needs administrator rights
PrivilegesRequired=admin
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
ChangesAssociations=yes
VersionInfoVersion={#Version}
VersionInfoDescription=Mend audio repair suite installer

[Types]
Name: "full"; Description: "Everything"
Name: "editor"; Description: "Mend Editor only"
Name: "plugins"; Description: "Plugins only (all formats)"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "editor"; Description: "Mend Editor"; Types: full editor
Name: "standalone"; Description: "Standalone app for each tool"; Types: full
Name: "vst3"; Description: "VST3 plugins"; Types: full plugins
Name: "clap"; Description: "CLAP plugins"; Types: full plugins
#if DirExists(Stage + "\LV2")
Name: "lv2"; Description: "LV2 plugins"; Types: full plugins
#endif

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut for Mend Editor"; Components: editor; Flags: unchecked
Name: "associate"; Description: "Open Mend projects (.mend) with Mend Editor"; Components: editor

[Files]
Source: "{#Stage}\MendEditor.exe"; DestDir: "{app}"; Components: editor; Flags: ignoreversion
Source: "{#Stage}\Standalone\*"; DestDir: "{app}\Standalone"; Components: standalone; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\VST3\*"; DestDir: "{commoncf64}\VST3\Mend"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Stage}\CLAP\*"; DestDir: "{commoncf64}\CLAP\Mend"; Components: clap; Flags: ignoreversion recursesubdirs createallsubdirs
#if DirExists(Stage + "\LV2")
; LV2 hosts only look one level deep, so the bundles go straight into LV2
Source: "{#Stage}\LV2\*"; DestDir: "{commoncf64}\LV2"; Components: lv2; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
Source: "{#Stage}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Stage}\README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "{#Stage}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion

[Icons]
Name: "{group}\Mend Editor"; Filename: "{app}\MendEditor.exe"; Components: editor
Name: "{autodesktop}\Mend Editor"; Filename: "{app}\MendEditor.exe"; Tasks: desktopicon
Name: "{group}\Tools\Mend Breath Control"; Filename: "{app}\Standalone\MendBreathControl.exe"; Components: standalone
Name: "{group}\Tools\Mend Centre Extract"; Filename: "{app}\Standalone\MendCentreExtract.exe"; Components: standalone
Name: "{group}\Tools\Mend De-click"; Filename: "{app}\Standalone\MendDeClick.exe"; Components: standalone
Name: "{group}\Tools\Mend De-clip"; Filename: "{app}\Standalone\MendDeClip.exe"; Components: standalone
Name: "{group}\Tools\Mend De-crackle"; Filename: "{app}\Standalone\MendDeCrackle.exe"; Components: standalone
Name: "{group}\Tools\Mend De-ess"; Filename: "{app}\Standalone\MendDeEss.exe"; Components: standalone
Name: "{group}\Tools\Mend De-hum"; Filename: "{app}\Standalone\MendDeHum.exe"; Components: standalone
Name: "{group}\Tools\Mend De-noise"; Filename: "{app}\Standalone\MendDeNoise.exe"; Components: standalone
Name: "{group}\Tools\Mend De-plosive"; Filename: "{app}\Standalone\MendDePlosive.exe"; Components: standalone
Name: "{group}\Tools\Mend De-reverb"; Filename: "{app}\Standalone\MendDeReverb.exe"; Components: standalone
Name: "{group}\Tools\Mend De-wind"; Filename: "{app}\Standalone\MendDeWind.exe"; Components: standalone
Name: "{group}\Tools\Mend EQ"; Filename: "{app}\Standalone\MendEq.exe"; Components: standalone
Name: "{group}\Tools\Mend Leveler"; Filename: "{app}\Standalone\MendLeveler.exe"; Components: standalone
Name: "{group}\Tools\Mend Voice"; Filename: "{app}\Standalone\MendVoice.exe"; Components: standalone
Name: "{group}\Uninstall Mend"; Filename: "{uninstallexe}"

[Registry]
Root: HKA; Subkey: "Software\Classes\.mend"; ValueType: string; ValueName: ""; ValueData: "Mend.Project"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Mend.Project"; ValueType: string; ValueName: ""; ValueData: "Mend project"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Mend.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\MendEditor.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Mend.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\MendEditor.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\MendEditor.exe"; Description: "Open Mend Editor"; Components: editor; Flags: nowait postinstall skipifsilent
