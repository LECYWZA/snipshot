cmake_minimum_required(VERSION 3.21)
project(snipshot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network DBus)

add_executable(snipshot
    src/main.cpp
    src/settings.cpp
    src/capture.cpp
    src/overlay.cpp
    src/session.cpp
    src/view.cpp
    src/clipboard.cpp
    src/recorder.cpp
    src/pin.cpp
    src/ai.cpp
    src/settingsdialog.cpp
    src/hotkeys.cpp
)

target_link_libraries(snipshot PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::DBus
)
