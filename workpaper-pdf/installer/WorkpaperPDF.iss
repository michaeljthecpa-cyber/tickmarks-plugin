; Workpaper PDF installer (Inno Setup 6). Installs for the current user only, so no admin rights are needed.
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\Release"
#endif

[Setup]
AppId={{6B0E3D52-4E0B-4C3E-9F0B-7A6A2B1D9C11}
AppName=Workpaper PDF
AppVersion={#AppVersion}
AppPublisher=Workpaper PDF
DefaultDirName={localappdata}\Programs\Workpaper PDF
DefaultGroupName=Workpaper PDF
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputBaseFilename=WorkpaperPDF-Setup
OutputDir=..\dist
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\res\app.ico
UninstallDisplayIcon={app}\WorkpaperPDF.exe
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked
Name: "pdfassoc"; Description: "Add Workpaper PDF to the ""Open with"" list for PDF files"

[Files]
Source: "{#SourceDir}\WorkpaperPDF.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\pdfium.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion
Source: "..\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{userprograms}\Workpaper PDF"; Filename: "{app}\WorkpaperPDF.exe"
Name: "{userdesktop}\Workpaper PDF"; Filename: "{app}\WorkpaperPDF.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Classes\WorkpaperPDF.Document"; ValueType: string; ValueData: "PDF Document"; Flags: uninsdeletekey; Tasks: pdfassoc
Root: HKCU; Subkey: "Software\Classes\WorkpaperPDF.Document\DefaultIcon"; ValueType: string; ValueData: "{app}\WorkpaperPDF.exe,0"; Tasks: pdfassoc
Root: HKCU; Subkey: "Software\Classes\WorkpaperPDF.Document\shell\open\command"; ValueType: string; ValueData: """{app}\WorkpaperPDF.exe"" ""%1"""; Tasks: pdfassoc
Root: HKCU; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "WorkpaperPDF.Document"; ValueData: ""; Flags: uninsdeletevalue; Tasks: pdfassoc
Root: HKCU; Subkey: "Software\Classes\Applications\WorkpaperPDF.exe\SupportedTypes"; ValueType: string; ValueName: ".pdf"; ValueData: ""; Flags: uninsdeletekey; Tasks: pdfassoc
; Lists the app under Settings > Apps > Default apps, so it can be made the default PDF app there.
Root: HKCU; Subkey: "Software\WorkpaperPDF\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Workpaper PDF"; Flags: uninsdeletekey; Tasks: pdfassoc
Root: HKCU; Subkey: "Software\WorkpaperPDF\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "A fast PDF app for audit workpapers"; Tasks: pdfassoc
Root: HKCU; Subkey: "Software\WorkpaperPDF\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "WorkpaperPDF.Document"; Tasks: pdfassoc
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Workpaper PDF"; ValueData: "Software\WorkpaperPDF\Capabilities"; Flags: uninsdeletevalue; Tasks: pdfassoc

[Run]
Filename: "{app}\WorkpaperPDF.exe"; Description: "Start Workpaper PDF now"; Flags: nowait postinstall skipifsilent
