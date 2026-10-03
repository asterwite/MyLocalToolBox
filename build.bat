@echo off
rem ToolBox Qt client build (qmake flow, relative paths to dodge the
rem moc/CJK-path bug: moc fails on absolute CJK paths, works with
rem relative args from the CJK cwd. Requires Qt 6.x MinGW kit.
rem Produces self-contained runnable folders (release\ and dist\)
rem plus ToolBoxQt-portable.zip. Deployment is done ONCE into dist,
rem verified, then mirrored into release -- never run windeployqt
rem twice independently (it failed silently once and left release
rem without the platforms folder -> 'no Qt platform plugin' error).
setlocal
set "PROJ=%~dp0."

set "QTPATH="
for /d %%D in ("D:\Qt\6.*") do if exist "%%D\mingw_64\bin\qmake.exe" set "QTPATH=%%D\mingw_64"
if "%QTPATH%"=="" for /d %%D in ("C:\Qt\6.*") do if exist "%%D\mingw_64\bin\qmake.exe" set "QTPATH=%%D\mingw_64"
if "%QTPATH%"=="" goto noqt
echo Qt found: %QTPATH%

set "MINGW="
for /d %%D in ("D:\Qt\Tools\mingw*") do if exist "%%D\bin\g++.exe" set "MINGW=%%D\bin"
if "%MINGW%"=="" for /d %%D in ("C:\Qt\Tools\mingw*") do if exist "%%D\bin\g++.exe" set "MINGW=%%D\bin"
if "%MINGW%"=="" goto nomingw
echo MinGW found: %MINGW%

set "PATH=%QTPATH%\bin;%MINGW%;%PATH%"
cd /d "%PROJ%"
if exist Makefile.Release del Makefile.Release >nul 2>&1

taskkill /IM ToolBoxQt.exe /F >nul 2>&1

qmake ToolBoxQt.pro
if errorlevel 1 goto fail
mingw32-make release -j4
if errorlevel 1 goto fail

rem ---- deploy runtime: windeployqt ONCE into dist, verify, mirror ----
if not exist dist mkdir dist
taskkill /IM ToolBoxQt.exe /F >nul 2>&1
copy /y release\ToolBoxQt.exe dist\ToolBoxQt.exe >nul 2>&1
"%QTPATH%\bin\windeployqt.exe" dist\ToolBoxQt.exe
if errorlevel 1 goto fail
copy /y "%MINGW%\libgcc_s_seh-1.dll" dist\ >nul 2>&1
copy /y "%MINGW%\libstdc++-6.dll" dist\ >nul 2>&1
copy /y "%MINGW%\libwinpthread-1.dll" dist\ >nul 2>&1
copy /y readme-dist.txt dist\README.txt >nul 2>&1
rem webp decoder plugin: Qt online installer does not ship qtimageformats;
rem qwebp.dll (Qt 6.11.2 mingw, from qtsdkrepository) is vendored in vendor\imageformats
copy /y vendor\imageformats\qwebp.dll dist\imageformats\ >nul 2>&1
if not exist dist\imageformats\qwebp.dll goto nowebp
if not exist dist\platforms\qwindows.dll goto nodeploy

rem OCR helper script (Windows built-in OCR via WinRT)
xcopy /E /I /Y tools dist\tools\ >nul 2>&1

rem refresh pet frames BEFORE mirroring, so release gets the new frames too
xcopy /E /I /Y pet-frames dist\pet-frames\ >nul 2>&1

rem mirror dist into release (additive, keeps build artifacts)
robocopy dist release /E /NFL /NDL /NJH /NJS >nul 2>&1
if errorlevel 8 goto fail
if not exist release\platforms\qwindows.dll goto nodeploy

powershell -NoProfile -Command "Compress-Archive -Path 'dist\*' -DestinationPath 'ToolBoxQt-portable.zip' -Force" >nul 2>&1
if not exist ToolBoxQt-portable.zip goto nozip

echo QT CLIENT OK: %PROJ%\dist\ToolBoxQt.exe
echo RUNNABLE ALSO: %PROJ%\release\ToolBoxQt.exe
echo PORTABLE ZIP: %PROJ%ToolBoxQt-portable.zip
exit /b 0

:nowebp
echo [ERROR] dist\imageformats\qwebp.dll missing: vendor\imageformats\qwebp.dll not found. Webp pet animations would not play.
pause
exit /b 1

:nodeploy
echo [ERROR] deployment incomplete: platforms\qwindows.dll missing after windeployqt/robocopy.
pause
exit /b 1

:nozip
echo [ERROR] portable zip creation failed.
pause
exit /b 1

:noqt
echo [ERROR] Qt 6.x MinGW kit not found (looked in D:\Qt and C:\Qt).
pause
exit /b 1

:nomingw
echo [ERROR] MinGW toolchain not found (install via Qt Maintenance Tool).
pause
exit /b 1

:fail
echo [ERROR] build failed
pause
exit /b 1
