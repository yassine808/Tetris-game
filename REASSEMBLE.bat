@echo off
rem ---------------------------------------------------------------------------
rem  REASSEMBLE.bat
rem
rem  The packaged game is published as a set of numbered .partNN files because
rem  GitHub rejects any single file over 100 MB. This script stitches them back
rem  into one .zip and extracts it, so you get a playable folder.
rem
rem  Usage:  put this file in the same folder as the .partNN files, then
rem          double-click it (or run it from a command prompt).
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "OUT=UNREAL-game.zip"

rem --- find how many parts there are -----------------------------------------
set COUNT=0
for %%F in ("UNREAL-game.zip.part*") do set /a COUNT+=1

if %COUNT%==0 (
    echo [ERROR] No UNREAL-game.zip.partNN files found in this folder.
    echo         Put this script in the same directory as the downloaded parts.
    pause
    exit /b 1
)

echo Found %COUNT% part file^(s^). Reassembling into %OUT% ...

rem --- concatenate in numeric order -------------------------------------------
del "%OUT%" >nul 2>&1
for /l %%I in (1,1,%COUNT%) do (
    set "P=UNREAL-game.zip.part%%I"
    if not exist "!P!" (
        echo [ERROR] Missing part %%I: !P!
        pause
        exit /b 1
    )
    copy /b "%OUT%"+"!P!" "%OUT%" >nul
    echo   + part %%I / %COUNT%
)

rem --- verify we produced a real zip ------------------------------------------
for %%A in ("%OUT%") do set SIZE=%%~zA
if %SIZE% LSS 1024 (
    echo [ERROR] Reassembled file is only %SIZE% bytes. The parts are incomplete.
    pause
    exit /b 1
)

echo.
echo Extracting %OUT% ...
powershell -NoProfile -Command "Expand-Archive -Path '%OUT%' -DestinationPath '.' -Force"
if errorlevel 1 (
    echo [ERROR] Extraction failed. The archive may be corrupt -- re-download
    echo         all part files and make sure none were renamed.
    pause
    exit /b 1
)

echo.
echo Done. Look for UNREAL-game.exe in the extracted folder and double-click it.
echo You can delete the .partNN files and %OUT% afterwards.
pause
