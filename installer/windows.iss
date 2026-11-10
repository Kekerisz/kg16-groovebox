; Windows installer for the KG-16 Groovebox VST3 (Inno Setup 6)
#define AppVer GetEnv("KG16_VERSION")
#if AppVer == ""
  #define AppVer "1.0.0"
#endif

[Setup]
AppName=KG-16 Groovebox
AppVersion={#AppVer}
AppPublisher=Kevin G Production
DefaultDirName={autopf}\KG-16 Groovebox
DefaultGroupName=KG-16 Groovebox
OutputBaseFilename=KG16-Groovebox-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "hungarian"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "..\build\KG16_artefacts\Release\VST3\KG-16 Groovebox.vst3\*"; DestDir: "{commoncf64}\VST3\KG-16 Groovebox.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\build\KG16_artefacts\Release\Standalone\KG-16 Groovebox.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\KG-16 Groovebox"; Filename: "{app}\KG-16 Groovebox.exe"
Name: "{group}\{cm:UninstallProgram,KG-16 Groovebox}"; Filename: "{uninstallexe}"
