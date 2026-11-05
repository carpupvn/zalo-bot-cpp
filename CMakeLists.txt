cmake_minimum_required(VERSION 3.20)

if(WIN32)
    set(CMAKE_RC_COMPILER "D:/CodeBlocks/MinGW/bin/windres.exe")
endif()

project(zalo-bot-cpp LANGUAGES CXX C)

# Tu dong cau hinh Boost
set(BOOST_ROOT "C:/msys64/ucrt64")
set(BOOST_INCLUDEDIR "C:/msys64/ucrt64/include")
set(BOOST_LIBRARYDIR "C:/msys64/ucrt64/lib")
find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/sqlite
    ${CMAKE_SOURCE_DIR}/include
)

set(SOURCES
    src/main.cpp
    src/ZaloApi.cpp
    src/ZaloBridge.cpp
    src/CommandHandler.cpp
    src/PermissionManager.cpp
    src/EconomyManager.cpp
    src/GameManager.cpp
    src/GroupManager.cpp
    src/Database.cpp
    src/RuntimeState.cpp
    src/AccountManager.cpp
    src/TextRenderer.cpp
    src/AssetManager.cpp
    src/EmojiRenderer.cpp
    src/NongTraiManager.cpp
    src/Color.cpp
    src/Gradient.cpp
    src/Shape.cpp
    src/Background.cpp
    src/Utils.cpp
    lib/sqlite/sqlite3.c
)

add_executable(zalo_bot ${SOURCES})

target_compile_options(zalo_bot PRIVATE
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
    -Wall
    -Wno-unused-parameter
)

target_compile_definitions(zalo_bot PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(zalo_bot PRIVATE
    winhttp
    secur32
    crypt32
    bcrypt
    ws2_32
    iphlpapi
    psapi
)

add_custom_command(TARGET zalo_bot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:zalo_bot>/data
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:zalo_bot>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:zalo_bot>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/bridge.js
            $<TARGET_FILE_DIR:zalo_bot>/bridge.js
    COMMENT "Copy bridge.js + assets"
)
