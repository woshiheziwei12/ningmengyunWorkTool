@echo off
setlocal

set "LEMON_HTTP_ENV=Dev2"
set "TOOL_DIR=%~dp0"
set "TOOL_EXE=%TOOL_DIR%LemonWorkToolCli.exe"

if exist "%TOOL_EXE%" (
    "%TOOL_EXE%" --http-env Dev2 %*
    exit /b %ERRORLEVEL%
)

set "RELEASE_EXE=%TOOL_DIR%Release\LemonWorkToolCli.exe"
if exist "%RELEASE_EXE%" (
    "%RELEASE_EXE%" --http-env Dev2 %*
    exit /b %ERRORLEVEL%
)

set "DEBUG_EXE=%TOOL_DIR%Debug\LemonWorkToolCli.exe"
if exist "%DEBUG_EXE%" (
    "%DEBUG_EXE%" --http-env Dev2 %*
    exit /b %ERRORLEVEL%
)

echo LemonWorkToolCli.exe not found. Build the LemonWorkToolCli project first.
exit /b 1
