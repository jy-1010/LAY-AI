@echo off
rem VS の「Developer Command Prompt」で実行してください
if not exist build mkdir build
cl /nologo /utf-8 /EHsc /std:c++17 /Fo:build\ /Fe:build\UnrailedDX11.exe src\main.cpp src\Renderer.cpp src\Game.cpp src\App.cpp src\Ui.cpp src\Input.cpp src\Settings.cpp src\KeyBindings.cpp src\Audio.cpp src\ObjLoader.cpp /link /SUBSYSTEM:WINDOWS
if errorlevel 1 exit /b 1
if not exist build\shaders mkdir build\shaders
copy /Y shaders\Instancing.hlsl build\shaders\ >nul
echo.
echo Build OK : build\UnrailedDX11.exe
