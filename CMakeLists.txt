cmake_minimum_required(VERSION 3.22)
project(radeon-profile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/components" "${CMAKE_CURRENT_SOURCE_DIR}/dialogs")

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Charts Concurrent)
find_package(Qt6 QUIET COMPONENTS LinguistTools)
find_package(X11 REQUIRED COMPONENTS Xrandr)
find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED IMPORTED_TARGET libdrm)

add_executable(radeon-profile
    main.cpp
    radeon_profile.cpp
    uiElements.cpp
    uiEvents.cpp
    gpu.cpp
    dxorg.cpp
    settings.cpp
    daemonComm.cpp
    ioctlHandler.cpp
    ioctl_radeon.cpp
    ioctl_amdgpu.cpp
    execbin.cpp
    tab_events.cpp
    tab_plots.cpp
    tab_fanControl.cpp
    tab_exec.cpp
    tab_overclock.cpp
    dialogs/dialog_defineplot.cpp
    dialogs/dialog_rpevent.cpp
    dialogs/dialog_topbarcfg.cpp
    dialogs/dialog_deinetopbaritem.cpp
    dialogs/dialog_sliders.cpp
    dialogs/dialog_plotsconfiguration.cpp
    components/fancurvechart.h
    components/rpplot.h
    components/pieprogressbar.h
    components/topbarcomponents.h
    components/slider.h
    radeon-resource.qrc
)
target_include_directories(radeon-profile PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(radeon-profile PRIVATE Qt6::Widgets Qt6::Network Qt6::Charts Qt6::Concurrent X11::Xrandr X11::X11 PkgConfig::DRM)
target_compile_options(radeon-profile PRIVATE -Wall -Wextra)

include(GNUInstallDirs)
install(TARGETS radeon-profile RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES extra/radeon-profile.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES extra/radeon-profile.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)

if(TARGET Qt6::lrelease)
    qt6_add_translation(QM_FILES
        translations/strings.hr.ts
        translations/strings.hu.ts
        translations/strings.it.ts
        translations/strings.pl.ts
        translations/strings.ru.ts
    )
    add_custom_target(translations ALL DEPENDS ${QM_FILES})
    install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/radeon-profile)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(system-info-test tests/system_info.cpp)
    target_link_libraries(system-info-test PRIVATE Qt6::Core)
    target_compile_options(system-info-test PRIVATE -UNDEBUG)
    add_test(NAME system-info COMMAND system-info-test)
    add_executable(fan-control-test tests/fan_control.cpp components/fancurvechart.h)
    target_link_libraries(fan-control-test PRIVATE Qt6::Widgets Qt6::Charts)
    target_compile_options(fan-control-test PRIVATE -UNDEBUG)
    add_test(NAME fan-control COMMAND fan-control-test)
    set_tests_properties(fan-control PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
