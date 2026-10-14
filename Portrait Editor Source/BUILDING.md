# Portrait Editor source

This is the current implementation of the Kenshi Portrait Editor plugin. The main file is `PortraitEditor.cpp`.

## Features

- Native portrait camera rotation, vertical/horizontal framing and zoom.
- Per-character settings stored in `%LOCALAPPDATA%/kenshi/portrait_editor.ini`.
- Native armor/equipment-triggered portrait regeneration.
- Solid backgrounds and user PNG/JPG/JPEG backgrounds.
- Background brightness and opacity; RGB controls for Custom colour only.
- Exact native portrait texture/UV capture and CPU extraction from the full atlas, followed by background composition.
- A compact Customize portrait button intended to appear above the normal portrait context menu.

## Build requirements

The implementation was built with the Visual C++ 2010 x64 toolchain and Windows SDK 7.1, against KenshiLib, Ogre and MyGUI libraries used for Kenshi 1.0.65 Steam. RE_Kenshi is required at runtime. External SDKs, libraries and headers are not bundled in this archive.

`build.ps1` expects a dependency directory with these existing folder names:

```
sdk71_vc/Program Files(64)/Microsoft Visual Studio 10.0/VC/
sdk71_vc_x86/Program Files/Microsoft Visual Studio 10.0/VC/
sdk71_core/Program Files/Microsoft SDKs/Windows/v7.1/
RE_Kenshi/KenshiLib/Include/
RE_Kenshi/KenshiLib/Libraries/ogre/
RE_Kenshi/KenshiLib/Libraries/mygui/
KenshiLib_Examples_deps/KenshiLib/Libraries/
KenshiLib_Examples_deps/boost_1_60_0/
```

The compiler executable is the amd64 `cl.exe` under the first folder. Link libraries are `KenshiLib.lib`, `OgreMain_x64.lib`, `MyGUIEngine_x64.lib` and `user32.lib`. Adjust paths in the script to match your environment while preserving ABI compatibility with the game.

Run from PowerShell:

```powershell
.\build.ps1 -DependencyRoot 'D:\your-kenshi-development-dependencies'
```

The DLL is generated inside `mod-template/`. Copy that folder's contents into `Kenshi/mods/Portrait Editor/` and enable `Portrait Editor.mod`. Close Kenshi before replacing its DLL.

## Background files

Place custom images in `mod-template/backgrounds/`; instructions are included there. The archive contains no user screenshot backgrounds.

## Implementation status

Camera controls, background identity/composition and armor updates were confirmed in-game by the user. The most recent context-menu placement change still needs confirmation. This remains an experimental native plugin tied to the supported executable and library ABI. A recent Ogre crash could not be attributed to a specific mod because its dump lacked a usable call stack.

This source package was assembled without changing or running the installed mod. Third-party dependencies retain their own licenses; this archive does not grant rights to redistribute those dependencies or the game.
