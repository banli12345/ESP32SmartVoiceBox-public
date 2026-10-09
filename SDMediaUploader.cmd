@echo off
cd /d "%~dp0"
if exist "dist\SDMediaUploader\SDMediaUploader.exe" (
    start "" "dist\SDMediaUploader\SDMediaUploader.exe"
) else (
    py -3 -m tools.sd_uploader.gui
    if errorlevel 1 pause
)
