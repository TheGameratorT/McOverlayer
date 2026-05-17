add_executable(mcoverlayer-cli main.cpp)

target_link_libraries(mcoverlayer-cli PRIVATE
    mcoverlayer-core
    Qt6::Core
    Qt6::Gui
)

include(GNUInstallDirs)
install(TARGETS mcoverlayer-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
