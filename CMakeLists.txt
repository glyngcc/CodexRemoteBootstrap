cmake_minimum_required(VERSION 3.16)

project(CodexRemoteBootstrap VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Concurrent Network)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appCodexRemoteBootstrap
    main.cpp
    bootstrapcontroller.cpp
    bootstrapcontroller.h
)

qt_add_qml_module(appCodexRemoteBootstrap
    URI CodexRemoteBootstrap
    VERSION 1.0
    QML_FILES
        Main.qml
)

set_target_properties(appCodexRemoteBootstrap PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appCodexRemoteBootstrap
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Concurrent
        Qt6::Network
)

include(GNUInstallDirs)
install(TARGETS appCodexRemoteBootstrap
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
