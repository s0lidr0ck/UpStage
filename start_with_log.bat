@echo off
cd /d "C:\projects\A18\UpStage\Builds\VisualStudio2022\x64\Debug\App"
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe" -g -G -o UpStage.exe 2>&1 | findstr /C:"DBG" > scan_log.txt
pause
