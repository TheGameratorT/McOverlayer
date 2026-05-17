set(GUI_SOURCES
    main.cpp
    FlowLayout.cpp
    AssignmentCard.cpp
    ThumbnailLoader.cpp
    PathConfigWidget.cpp
    ConfigPanel.cpp
    ApplyDialog.cpp
    OverlayLookupDialog.cpp
    SeedSearchDialog.cpp
    MainWindow.cpp
)

add_executable(mcoverlayer-gui ${GUI_SOURCES})

target_link_libraries(mcoverlayer-gui PRIVATE
    mcoverlayer-core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
)

if(APPLE)
    set_target_properties(mcoverlayer-gui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "MCOverlayer"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
endif()

include(GNUInstallDirs)
install(TARGETS mcoverlayer-gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .
)
