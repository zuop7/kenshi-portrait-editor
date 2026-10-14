param([Parameter(Mandatory=$true)][string]$DependencyRoot)
$workspace = [System.IO.Path]::GetFullPath($DependencyRoot)
New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'build') -Force | Out-Null
$vc64 = Join-Path $workspace 'sdk71_vc\Program Files(64)\Microsoft Visual Studio 10.0\VC'
$vcHeaders = Join-Path $workspace 'sdk71_vc_x86\Program Files\Microsoft Visual Studio 10.0\VC'
$winSDK = Join-Path $workspace 'sdk71_core\Program Files\Microsoft SDKs\Windows\v7.1'
$kenshiHeaders = Join-Path $workspace 'RE_Kenshi\KenshiLib\Include'
$env:INCLUDE = "$vcHeaders\include;$winSDK\Include;$kenshiHeaders;$kenshiHeaders\ogre;$workspace\KenshiLib_Examples_deps\boost_1_60_0"
$env:LIB = "$vc64\lib\amd64;$winSDK\Lib\x64;$workspace\KenshiLib_Examples_deps\KenshiLib\Libraries;$workspace\RE_Kenshi\KenshiLib\Libraries\ogre;$workspace\RE_Kenshi\KenshiLib\Libraries\mygui"
$env:PATH = "$vc64\bin\amd64;$vcHeaders\bin;$workspace\sdk71_vc\Win\System64;$env:PATH"
$destination = Join-Path $PSScriptRoot 'mod-template\PortraitEditor.dll'
& "$vc64\bin\amd64\cl.exe" /nologo /EHsc /MD /O2 /GL /LD /DNDEBUG /DUNICODE /D_UNICODE /DBOOST_ALL_NO_LIB /DBOOST_ERROR_CODE_HEADER_ONLY "/FI$kenshiHeaders\kenshi\util\OgreUnordered.h" "$PSScriptRoot\PortraitEditor.cpp" "/Fo$PSScriptRoot\build\PortraitEditor.obj" /link /LTCG KenshiLib.lib OgreMain_x64.lib MyGUIEngine_x64.lib user32.lib "/OUT:$destination"
exit $LASTEXITCODE




