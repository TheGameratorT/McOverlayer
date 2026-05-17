add_library(mcoverlayer-core
    src/Hash.cpp
    src/FileUtils.cpp
    src/AnimationUtils.cpp
    src/EntityLoader.cpp
    src/PathConfig.cpp
    src/OverlayMapper.cpp
    src/EntityMapper.cpp
    src/ImageCache.cpp
    src/ImageProcessor.cpp
    src/MappingConfig.cpp
)

target_include_directories(mcoverlayer-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(mcoverlayer-core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Concurrent
)

set_target_properties(mcoverlayer-core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

include(GNUInstallDirs)
install(TARGETS mcoverlayer-core
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
