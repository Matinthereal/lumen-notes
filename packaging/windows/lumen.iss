; Inno Setup script for the Windows installer. CI (.github/workflows/build.yml) passes
; /DLumenInstallDir pointing at a `cmake --install` + windeployqt tree (lumen.exe, workers/,
; prompts/, docs, Qt DLLs and QML modules, all flat -- see the WIN32 branch in the top-level
; CMakeLists.txt and workersDir()/promptsDir() in the C++, which look next to the exe for exactly
; this layout). Build locally with `iscc packaging\windows\lumen.iss` after an install + windeployqt.
#ifndef LumenInstallDir
#define LumenInstallDir "..\..\install"
#endif

#define MyAppName "Lumen"
#define MyAppVersion "0.0.1"
#define MyAppExeName "lumen.exe"
#define MyAppPublisher "Lumen"

[Setup]
AppId={{6C3B6C0E-6C0F-4E2D-9A9B-6B7F6D0B0C2A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
OutputBaseFilename=Lumen-Setup-{#MyAppVersion}
OutputDir=Output
Compression=lzma2
SolidCompression=yes
SetupIconFile=lumen.ico
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: unchecked

[Files]
Source: "{#LumenInstallDir}\*"; DestDir: "{app}"; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent unchecked
