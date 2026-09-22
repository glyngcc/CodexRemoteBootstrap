cmake_minimum_required(VERSION 3.16)

# Single source of truth for the app version. GitHub Release tags must match
# this value (tag v0.4.0 == VERSION 0.4.0).
project(CodexRemoteBootstrap VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 Concurrent Network)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appCodexRemoteBootstrap
    src/main.cpp
    src/bootstrapcontroller.cpp
    src/bootstrapcontroller.h
)

set_source_files_properties(qml/Main.qml PROPERTIES
    QT_RESOURCE_ALIAS Main.qml
)

qt_add_qml_module(appCodexRemoteBootstrap
    URI CodexRemoteBootstrap
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
    @ONLY
)

set_target_properties(appCodexRemoteBootstrap PROPERTIES
    OUTPUT_NAME CodexRemoteBootstrap
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_include_directories(appCodexRemoteBootstrap
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
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
