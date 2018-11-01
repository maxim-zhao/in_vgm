@echo off
echo Copying DLL... >con
copy "c:\Program Files\Winamp\Plugins\in_vgm.dll" .
echo Compressing it... >con
upx --best -f in_vgm.dll
echo Zipping DLL and readme... >con
"c:\program files\winrar\winrar.exe" a -m5 in_vgm.zip in_vgm.dll in_vgm.html
echo Zipping source... >con
"c:\program files\winrar\winrar.exe" a -m5 -x*.svn-base -x*.svn -x*\format -x*\entries -x*\all-wcprops -x*\dir-prop-base -x*Thumbs.db in_vgmsrc.zip in_vgm.c emu2413 sn76489 html*.txt mainicon.ico *.bmp *.sln *.vc* *.suo in_vgm*.txt *.html mame gens images mame_ym2612_emu
echo NSISing...
"c:\program files\nsis\makensis.exe" in_vgm.nsi
echo Deleting DLL... >con
copy in_vgm.dll "c:\program files\winamp\plugins"
del in_vgm.dll
echo Finished! >con
pause
