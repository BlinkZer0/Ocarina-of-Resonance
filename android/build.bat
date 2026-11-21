@echo off
setlocal
if not defined ANDROID_HOME if exist "%LOCALAPPDATA%\Android\Sdk" set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
if "%~1"=="" (
    call "%~dp0gradlew.bat" -p "%~dp0." assembleDebug
) else (
    call "%~dp0gradlew.bat" -p "%~dp0." %*
)
exit /b %errorlevel%
