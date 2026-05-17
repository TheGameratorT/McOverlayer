set(RE_SOURCES
    main.cpp
    TextureCanvas.cpp
    RegionEditorWindow.cpp
)

add_executable(mcoverlayer-region-editor ${RE_SOURCES})

target_link_libraries(mcoverlayer-region-editor PRIVATE
    mcoverlayer-core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

if(APPLE)
    set_target_properties(mcoverlayer-region-editor PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "MCOverlayer Region Editor"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
endif()

include(GNUInstallDirs)
install(TARGETS mcoverlayer-region-editor
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .
)
