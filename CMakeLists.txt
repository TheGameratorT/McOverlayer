cmake_minimum_required(VERSION 3.20)
project(MCOverlayer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# Auto-export all symbols on Windows so no __declspec(dllexport) annotations are needed
if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# RPATH for installed binaries (so they find the shared lib without LD_LIBRARY_PATH)
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
endif()
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_SKIP_BUILD_RPATH FALSE)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
qt_standard_project_setup()

add_subdirectory(libs/core)
add_subdirectory(apps/cli)
add_subdirectory(apps/gui)
add_subdirectory(apps/region_editor)

include(GNUInstallDirs)
if(UNIX AND NOT APPLE)
    install(DIRECTORY entity_regions/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/MCOverlayer/entity_regions
    )
endif()

# === Deploy target ===
set(DIST_DIR "${CMAKE_BINARY_DIR}/dist")

get_target_property(QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
get_filename_component(QT_BIN_DIR "${QMAKE_EXECUTABLE}" DIRECTORY)

add_custom_target(deploy ALL
    DEPENDS mcoverlayer-cli mcoverlayer-gui mcoverlayer-region-editor
    COMMENT "Deploying to dist..."
)

add_custom_command(TARGET deploy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/entity_regions ${DIST_DIR}/entity_regions
    COMMENT "Setting up dist directory..."
)

if(APPLE)
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcoverlayer-cli> ${DIST_DIR}/
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            $<TARGET_BUNDLE_DIR:mcoverlayer-gui> ${DIST_DIR}/mcoverlayer-gui.app
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            $<TARGET_BUNDLE_DIR:mcoverlayer-region-editor> ${DIST_DIR}/mcoverlayer-region-editor.app
        COMMENT "Copying macOS bundles to dist..."
    )
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${QT_BIN_DIR}")
    if(MACDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET deploy POST_BUILD
            COMMAND ${MACDEPLOYQT_EXECUTABLE} ${DIST_DIR}/mcoverlayer-gui.app
            COMMAND ${MACDEPLOYQT_EXECUTABLE} ${DIST_DIR}/mcoverlayer-region-editor.app
            COMMENT "Running macdeployqt..."
        )
    else()
        message(WARNING "macdeployqt not found. Qt frameworks will need to be bundled manually.")
    endif()
else()
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcoverlayer-core> ${DIST_DIR}/
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcoverlayer-cli> ${DIST_DIR}/
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcoverlayer-gui> ${DIST_DIR}/
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mcoverlayer-region-editor> ${DIST_DIR}/
        COMMENT "Copying executables to dist..."
    )
    if(WIN32)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET deploy POST_BUILD
                COMMAND ${WINDEPLOYQT_EXECUTABLE} --no-translations --no-system-d3d-compiler --no-opengl-sw
                        "${DIST_DIR}/$<TARGET_FILE_NAME:mcoverlayer-gui>"
                COMMAND ${WINDEPLOYQT_EXECUTABLE} --no-translations --no-system-d3d-compiler --no-opengl-sw
                        "${DIST_DIR}/$<TARGET_FILE_NAME:mcoverlayer-region-editor>"
                COMMAND ${WINDEPLOYQT_EXECUTABLE} --no-translations --no-system-d3d-compiler --no-opengl-sw
                        "${DIST_DIR}/$<TARGET_FILE_NAME:mcoverlayer-cli>"
                COMMENT "Running windeployqt..."
            )
        else()
            message(WARNING "windeployqt not found. Qt DLLs will need to be copied manually.")
        endif()
        if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_custom_command(TARGET deploy POST_BUILD
                COMMAND bash -c "ldd ${DIST_DIR}/$<TARGET_FILE_NAME:mcoverlayer-gui> | grep /mingw64 | awk '{print $$3}' | xargs -I {} cp -u {} ${DIST_DIR}/ || true"
                COMMAND bash -c "ldd ${DIST_DIR}/$<TARGET_FILE_NAME:mcoverlayer-region-editor> | grep /mingw64 | awk '{print $$3}' | xargs -I {} cp -u {} ${DIST_DIR}/ || true"
                COMMAND bash -c "ldd ${DIST_DIR}/$<TARGET_FILE_NAME:mcoverlayer-cli> | grep /mingw64 | awk '{print $$3}' | xargs -I {} cp -u {} ${DIST_DIR}/ || true"
                COMMENT "Copying MinGW DLLs..."
            )
        endif()
    endif()
endif()
