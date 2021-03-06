#We need cmake 3.0 or later because of the autouic, autorcc reatures for Qt
cmake_minimum_required(VERSION 3.0)
project(rtctestapp)

FIND_PACKAGE(Qt5Widgets REQUIRED)

set(CMAKE_MACOSX_RPATH 1)
#set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(../../src karere)

set (SRCS
     mainwindow.cpp
     chatWindow.cpp
     main.cpp
#need to include also headers with Q_OBJECTs so that automoc knows to parse them
     widgetSubclass.h
#==
     res/resources.qrc
)
if (NOT optKarereDisableWebrtc)
    list(APPEND SRCS callGui.cpp ../../src/videoRenderer_Qt.cpp)
endif()


if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_definitions(-D_DEBUG -fvisibility=hidden -Wall -Wno-unused-local-typedef)
    if (optAsanMode AND ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")) # defined by Karere
        add_definitions(-fsanitize=${optAsanMode} -fno-omit-frame-pointer) #enable ASAN
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=${optAsanMode}")
    endif()
#    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()

get_property(KARERE_INCLUDE_DIRS GLOBAL PROPERTY KARERE_INCLUDE_DIRS)
include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${KARERE_INCLUDE_DIRS})

get_property(KARERE_DEFINES GLOBAL PROPERTY KARERE_DEFINES)
add_definitions(${KARERE_DEFINES})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(SYSLIBS)
if (CLANG_STDLIB)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=lib${CLANG_STDLIB}")
    set(SYSLIBS ${CLANG_STDLIB})
endif()

add_executable(rtctestapp MACOSX_BUNDLE ${SRCS})
set_target_properties(rtctestapp PROPERTIES AUTOMOC TRUE AUTOUIC TRUE AUTORCC TRUE)
target_link_libraries(rtctestapp
    Qt5::Widgets
    karere
    ${SYSLIBS}
)

get_target_property(QMAKE_EXECUTABLE Qt5::qmake LOCATION)
execute_process(COMMAND ${QMAKE_EXECUTABLE} -query QT_INSTALL_PLUGINS
    OUTPUT_VARIABLE QT_PLUGINS_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if (APPLE) # AND ("${CMAKE_BUILD_TYPE}" STREQUAL "Release"))
    set_target_properties(rtctestapp PROPERTIES
        MACOSX_RPATH ON
        INSTALL_RPATH "@executable_path")

    set_target_properties(rtctestapp PROPERTIES
        MACOSX_BUNDLE_INFO_STRING "${PROJECT_NAME}, Copyright 2016 Mega LTD."
        MACOSX_BUNDLE_ICON_FILE ""
        MACOSX_BUNDLE_GUI_IDENTIFIER "${megachat-test}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${MACOSX_BUNDLE_INFO_STRING}"
        MACOSX_BUNDLE_BUNDLE_NAME "${megachat-test}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION_STRING}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${VERSION_STRING}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright 2016, MEGA Ltd. All Rights Reserved."
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/MacOSXBundleInfo.plist.in"
    )

    INSTALL(FILES
       "${QT_PLUGINS_DIR}/platforms/libqcocoa.dylib"
       DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/rtctestapp.app/Contents/Plugins/platforms
       COMPONENT Runtime)

    INSTALL(FILES
       "${QT_PLUGINS_DIR}/imageformats/libqsvg.dylib"
       "${QT_PLUGINS_DIR}/imageformats/libqicns.dylib"
       DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/rtctestapp.app/Contents/Plugins/imageformats
       COMPONENT Runtime)

    INSTALL(CODE "
       include(BundleUtilities)
       set(BU_CHMOD_BUNDLE_ITEMS ON)
       set(plugins
           ${CMAKE_CURRENT_BINARY_DIR}/rtctestapp.app/Contents/Plugins/platforms/libqcocoa.dylib
           ${CMAKE_CURRENT_BINARY_DIR}/rtctestapp.app/Contents/Plugins/imageformats/libqsvg.dylib
           ${CMAKE_CURRENT_BINARY_DIR}/rtctestapp.app/Contents/Plugins/imageformats/libqicns.dylib
       )

    fixup_bundle(
           \"${CMAKE_CURRENT_BINARY_DIR}/rtctestapp.app\" \"\${plugins}\" \"\")"
           COMPONENT Runtime)

    INSTALL(TARGETS rtctestapp BUNDLE DESTINATION . COMPONENT Runtime)

    set(CPACK_BINARY_DRAGNDROP ON)
    set(CPACK_OSX_PACKAGE_VERSION 10.10)
    include(CPack)
elseif(UNIX)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/dist")
    INSTALL(TARGETS rtctestapp DESTINATION "${CMAKE_INSTALL_PREFIX}" COMPONENT Runtime)
    INSTALL(FILES
       "${QT_PLUGINS_DIR}/platforms/libqxcb.so"
       DESTINATION ${CMAKE_INSTALL_PREFIX}/QtPlugins/platforms
       COMPONENT Runtime)

    INSTALL(FILES
       "${QT_PLUGINS_DIR}/imageformats/libqsvg.so"
       DESTINATION ${CMAKE_INSTALL_PREFIX}/QtPlugins/imageformats
       COMPONENT Runtime)
#    INSTALL(CODE
#    "
#        include(BundleUtilities)
#        set(BU_CHMOD_BUNDLE_ITEMS ON)
#        set(plugins
#            ${CMAKE_INSTALL_PREFIX}/QtPlugins/platforms/libqxcb.so
#            ${CMAKE_INSTALL_PREFIX}/QtPlugins/imageformats/libqsvg.so
#        )
#        fixup_bundle(\"\${CMAKE_INSTALL_PREFIX}/rtctestapp\" \"\${plugins}\" \"\")
#    "
#    COMPONENT Runtime)

    include(CPack)
endif()
