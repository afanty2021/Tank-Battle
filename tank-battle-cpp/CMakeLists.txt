cmake_minimum_required(VERSION 3.21)
project(tank-battle LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SFML 3 REQUIRED COMPONENTS Graphics Audio Window System)

# exe 图标资源(仅 Windows; windres 经 include 路径 res/ 查找 tank.ico)
set(TB_SOURCES src/main.cpp src/Assets.cpp src/Game.cpp)
if(WIN32)
    list(APPEND TB_SOURCES res/app_icon.rc)
endif()

add_executable(tank-battle ${TB_SOURCES})
target_link_libraries(tank-battle PRIVATE
    SFML::Graphics SFML::Audio SFML::Window SFML::System)
if(WIN32)
    target_include_directories(tank-battle PRIVATE res)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tank-battle PRIVATE -Wall -Wextra -Wshadow -Wconversion)
endif()

add_executable(unit_tests tests/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE SFML::System)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(unit_tests PRIVATE -Wall -Wextra -Wshadow -Wconversion)
endif()

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)

# MSYS2/MinGW: 把依赖 DLL 拷到 exe 旁边(本机还需 -B 提示的工具目录与
# cc1 旁的 DLL, 见 build.bat 注释; CMake 路径用于 IDE 场景)
if(WIN32 AND NOT MSVC)
    add_custom_command(TARGET tank-battle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:tank-battle>
            $<TARGET_FILE_DIR:tank-battle>
        COMMAND_EXPAND_LISTS)
endif()
