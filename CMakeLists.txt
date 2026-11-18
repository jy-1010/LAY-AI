cmake_minimum_required(VERSION 3.16)
project(UnrailedDX11 CXX)
set(CMAKE_CXX_STANDARD 17)
if(MSVC)
  add_compile_options(/utf-8)
endif()

add_executable(UnrailedDX11 WIN32
    src/main.cpp src/Renderer.cpp src/App.cpp src/Ui.cpp src/Game.cpp src/Input.cpp src/Settings.cpp src/Audio.cpp src/ObjLoader.cpp
    src/Renderer.h src/Config.h src/MeshGen.h src/App.h src/Ui.h src/Game.h src/Input.h src/Settings.h src/Upgrade.h src/Audio.h src/Mesh.h src/MeshGen.h)
target_link_libraries(UnrailedDX11 PRIVATE d3d11 d3dcompiler user32 xinput xaudio2 ole32)

add_custom_command(TARGET UnrailedDX11 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:UnrailedDX11>/shaders)
