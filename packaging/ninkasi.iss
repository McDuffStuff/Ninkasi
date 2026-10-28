; Inno Setup script for Ninkasi's Windows installer (jrsoftware.org/isinfo.php).
; scripts\package.ps1 runs it with the version and the folder of files to
; install, so build installers with that rather than by hand:
;   ISCC /DAppVersion=1.0.0 /DPublisher="..." /DStageDir=... /DOutputDir=... ninkasi.iss
; With /DIconFile=..., the installer wears Ninkasi's icon.

#ifndef AppVersion
  #error Build the installer with scripts\package.ps1, which passes the version.
#endif

[Setup]
; Every version keeps this id, so installing a new one replaces the old.
AppId={{9293E48F-5542-42BB-9182-5ED81310E78B}
AppName=Ninkasi
AppVersion={#AppVersion}
AppVerName=Ninkasi {#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL=https://github.com/McDuffStuff/Ninkasi
VersionInfoVersion={#AppVersion}
; For the person installing it, with no administrator rights needed
; (%LOCALAPPDATA%\Programs\Ninkasi); someone with them can install it for everyone.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\Ninkasi
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=Ninkasi-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\ninkasi.exe
#ifdef IconFile
SetupIconFile={#IconFile}
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\ninkasi.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Ninkasi"; Filename: "{app}\ninkasi.exe"
Name: "{autodesktop}\Ninkasi"; Filename: "{app}\ninkasi.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\ninkasi.exe"; Description: "{cm:LaunchProgram,Ninkasi}"; Flags: nowait postinstall skipifsilent

; The brewery's data is in %APPDATA%\Ninkasi, not here, so uninstalling
; leaves it in place and reinstalling picks it up again.
