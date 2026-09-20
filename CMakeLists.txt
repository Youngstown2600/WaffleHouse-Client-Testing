cmake_minimum_required(VERSION 3.20)

project(WaffleHouseClient VERSION 5.6.0 LANGUAGES CXX)

set(APP_DISPLAY_NAME "WaffleHouse-Client" CACHE STRING "Application display name")
set(APP_ID "WaffleHouseClient" CACHE STRING "Application settings/config identifier")
set(APP_EXECUTABLE "wafflehouse-client" CACHE STRING "Executable name")

option(WAFFLEHOUSE_ENABLE_OSCAR "Build AIM/OSCAR support" ON)
option(WAFFLEHOUSE_ENABLE_IRC "Build IRC support" ON)
option(WAFFLEHOUSE_ENABLE_TELNET "Build Telnet/BBS support" ON)
option(WAFFLEHOUSE_ENABLE_SIP "Build SIP/VoIP support" ON)
option(WAFFLEHOUSE_ENABLE_MEDIA "Build media/radio UI support" ON)
option(WAFFLEHOUSE_ENABLE_XMPP "Build XMPP/Jabber support" ON)
option(WAFFLEHOUSE_ENABLE_SSH "Build SSH support" ON)
option(WAFFLEHOUSE_ENABLE_NNTP "Build NNTP/Usenet support" ON)
option(WAFFLEHOUSE_ENABLE_MOSH "Build Mosh launcher module" ON)
option(WAFFLEHOUSE_ENABLE_GOPHER "Build Gopher browser module" ON)
option(WAFFLEHOUSE_ENABLE_GEMINI "Build Gemini browser module" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

set(WAFFLEHOUSE_TERMUX_BUILD OFF)
if(DEFINED ENV{TERMUX_VERSION} OR "$ENV{PREFIX}" MATCHES "com[.]termux")
    set(WAFFLEHOUSE_TERMUX_BUILD ON)
endif()

find_package(Threads REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Multimedia)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)
# X11/Wayland keyboard integration where available. It is not required by
# Windows, macOS/Cocoa, or Termux/X11 builds.
if(UNIX AND NOT APPLE AND NOT WAFFLEHOUSE_TERMUX_BUILD)
    pkg_check_modules(XKBCOMMON QUIET IMPORTED_TARGET xkbcommon)
endif()

# The integrated softphone uses the 3.5/r14 synchronized PJSIP 2.17 engine.
# 5.1 keeps the same tested PJSIP ABI while exposing the shared GUI/CLI feature set on all supported platforms.
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(ENV{PKG_CONFIG_ALLOW_SYSTEM_LIBS} "1")
endif()
pkg_check_modules(PJPROJECT REQUIRED libpjproject)
if(NOT PJPROJECT_VERSION VERSION_EQUAL "2.17")
    message(FATAL_ERROR "WaffleHouse-Client 5.6 softphone requires PJSIP 2.17; pkg-config reported ${PJPROJECT_VERSION}")
endif()
if(NOT PJPROJECT_STATIC_LDFLAGS)
    message(FATAL_ERROR "pkg-config did not return static PJSIP flags. Check: pkg-config --cflags --libs --static libpjproject")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    string(JOIN " " _wh_pj_static_flags ${PJPROJECT_STATIC_LDFLAGS})
    string(FIND " ${_wh_pj_static_flags} " " -lstdc++ " _wh_stdcxx_pos)
    if(NOT _wh_stdcxx_pos EQUAL -1)
        message(FATAL_ERROR "FreeBSD PJSIP advertises libstdc++; rebuild the managed PJSIP with ./build.sh --pjsip so it uses base Clang/libc++.")
    endif()
    string(FIND " ${_wh_pj_static_flags} " " -lc++ " _wh_libcxx_pos)
    if(_wh_libcxx_pos EQUAL -1)
        message(FATAL_ERROR "FreeBSD PJSIP does not advertise libc++ linkage; rebuild with ./build.sh --pjsip.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "FreeBSD WaffleHouse-Client must use base Clang/libc++ for Qt/PJSIP ABI compatibility.")
    endif()
endif()

qt_standard_project_setup()
include(GNUInstallDirs)

add_library(wafflehouse_sipcore
    src/sipcore/Profile.cpp
    src/sipcore/TextPool.cpp
    src/sipcore/Logger.cpp
    src/sipcore/SipTrace.cpp
    src/sipcore/CallSession.cpp
    src/sipcore/SipAccount.cpp
    src/sipcore/SipEngine.cpp
    src/sipcore/SipWireMonitor.cpp
    src/sipcore/RuntimePaths.cpp
    src/sipcore/RemoteSipAudio.cpp
)
target_include_directories(wafflehouse_sipcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PJPROJECT_INCLUDE_DIRS}
)
target_compile_options(wafflehouse_sipcore PUBLIC ${PJPROJECT_CFLAGS_OTHER})
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    if(NOT WAFFLEHOUSE_FREEBSD_LOCALBASE)
        if(DEFINED ENV{LOCALBASE} AND NOT "$ENV{LOCALBASE}" STREQUAL "")
            set(WAFFLEHOUSE_FREEBSD_LOCALBASE "$ENV{LOCALBASE}")
        else()
            set(WAFFLEHOUSE_FREEBSD_LOCALBASE "/usr/local")
        endif()
    endif()
    target_link_directories(wafflehouse_sipcore PUBLIC "${WAFFLEHOUSE_FREEBSD_LOCALBASE}/lib")
endif()
# pkg-config returns macOS frameworks as a two-token linker pair, e.g.
#   -framework;CoreServices
# Passing that raw list to target_link_libraries() makes CMake treat the framework
# name as a normal library and emit -lCoreServices. Split framework pairs out and
# forward them as linker options so AppleClang receives the required
# "-framework <Name>" form. Keep every non-framework PJSIP static linker token
# unchanged for Linux/FreeBSD.
set(WAFFLEHOUSE_PJPROJECT_STATIC_LINK_ITEMS)
set(WAFFLEHOUSE_PJPROJECT_FRAMEWORK_OPTIONS)
set(_wh_expect_pj_framework FALSE)
foreach(_wh_pj_link_item IN LISTS PJPROJECT_STATIC_LDFLAGS)
    if(APPLE AND _wh_expect_pj_framework)
        list(APPEND WAFFLEHOUSE_PJPROJECT_FRAMEWORK_OPTIONS
            "SHELL:-framework ${_wh_pj_link_item}")
        set(_wh_expect_pj_framework FALSE)
    elseif(APPLE AND _wh_pj_link_item STREQUAL "-framework")
        set(_wh_expect_pj_framework TRUE)
    elseif(APPLE AND _wh_pj_link_item MATCHES "^-framework[ \t]+(.+)$")
        list(APPEND WAFFLEHOUSE_PJPROJECT_FRAMEWORK_OPTIONS
            "SHELL:-framework ${CMAKE_MATCH_1}")
    else()
        list(APPEND WAFFLEHOUSE_PJPROJECT_STATIC_LINK_ITEMS "${_wh_pj_link_item}")
    endif()
endforeach()
if(_wh_expect_pj_framework)
    message(FATAL_ERROR "PJSIP pkg-config metadata ended with -framework but no framework name")
endif()

target_link_libraries(wafflehouse_sipcore PUBLIC
    ${WAFFLEHOUSE_PJPROJECT_STATIC_LINK_ITEMS}
    Threads::Threads
)

set(WAFFLEHOUSE_SHARED_SOURCES
    src/mediacontroller.cpp
    src/backend.cpp
    src/ircbackend.cpp
    src/xmppbackend.cpp
    src/sshbackend.cpp
    src/nntpbackend.cpp
    src/oscarprotocol.cpp
    src/oscarbackend.cpp
    src/oscarvoice.cpp
    src/telnetbackend.cpp
    src/ansiterminal.cpp
    src/bbsdirectory.cpp
    src/securechannel.cpp
    src/secureroom.cpp
    src/filetransfer.cpp
    src/directtransfer.cpp
    src/platforminfo.cpp
    src/notificationmanager.cpp
    src/useractivity.cpp
    src/terminalui.cpp
    src/sipcontroller.cpp
    src/sipbackend.cpp
    src/core/capabilityregistry.cpp
    src/core/contactstore.cpp
    src/core/historystore.cpp
)

qt_add_executable(${APP_EXECUTABLE}
    src/main.cpp
    ${WAFFLEHOUSE_SHARED_SOURCES}
    src/ansiterminalwidget.cpp
    src/chatwindow.cpp
    src/mainwindow.cpp
    src/modernstyle.cpp
    src/mediawindow.cpp
    src/wafflecast.cpp
    src/transferwindow.cpp
    src/softphonewindow.cpp
    src/networktoolswindow.cpp
)

qt_add_resources(${APP_EXECUTABLE} wafflehouse_branding
    PREFIX "/branding"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding"
    FILES assets/branding/wafflehouse-client-logo.png
)
qt_add_resources(${APP_EXECUTABLE} wafflehouse_icons
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons"
    FILES
        assets/icons/16x16/wafflehouse-client.png
        assets/icons/22x22/wafflehouse-client.png
        assets/icons/24x24/wafflehouse-client.png
        assets/icons/32x32/wafflehouse-client.png
        assets/icons/48x48/wafflehouse-client.png
        assets/icons/64x64/wafflehouse-client.png
        assets/icons/128x128/wafflehouse-client.png
        assets/icons/256x256/wafflehouse-client.png
        assets/icons/512x512/wafflehouse-client.png
)


target_include_directories(${APP_EXECUTABLE} PRIVATE src include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(${APP_EXECUTABLE} PRIVATE
    APP_DISPLAY_NAME="${APP_DISPLAY_NAME}"
    APP_ID="${APP_ID}"
    APP_EXECUTABLE="${APP_EXECUTABLE}"
    APP_VERSION_STRING="5.6"
    WAFFLEHOUSE_SOURCE_SOUND_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sounds"
    WAFFLEHOUSE_OSCAR_VOICE=1
    WAFFLEHOUSE_FEATURE_OSCAR=$<BOOL:${WAFFLEHOUSE_ENABLE_OSCAR}>
    WAFFLEHOUSE_FEATURE_IRC=$<BOOL:${WAFFLEHOUSE_ENABLE_IRC}>
    WAFFLEHOUSE_FEATURE_TELNET=$<BOOL:${WAFFLEHOUSE_ENABLE_TELNET}>
    WAFFLEHOUSE_FEATURE_SIP=$<BOOL:${WAFFLEHOUSE_ENABLE_SIP}>
    WAFFLEHOUSE_FEATURE_MEDIA=$<BOOL:${WAFFLEHOUSE_ENABLE_MEDIA}>
    WAFFLEHOUSE_FEATURE_XMPP=$<BOOL:${WAFFLEHOUSE_ENABLE_XMPP}>
    WAFFLEHOUSE_FEATURE_SSH=$<BOOL:${WAFFLEHOUSE_ENABLE_SSH}>
    WAFFLEHOUSE_FEATURE_NNTP=$<BOOL:${WAFFLEHOUSE_ENABLE_NNTP}>
    WAFFLEHOUSE_FEATURE_MOSH=$<BOOL:${WAFFLEHOUSE_ENABLE_MOSH}>
    WAFFLEHOUSE_FEATURE_GOPHER=$<BOOL:${WAFFLEHOUSE_ENABLE_GOPHER}>
    WAFFLEHOUSE_FEATURE_GEMINI=$<BOOL:${WAFFLEHOUSE_ENABLE_GEMINI}>
)
target_compile_definitions(${APP_EXECUTABLE} PRIVATE WAFFLEHOUSE_GUI=1)

target_link_libraries(${APP_EXECUTABLE} PRIVATE
    wafflehouse_sipcore
    Qt6::Core
    Qt6::Network
    Qt6::Multimedia
    PkgConfig::SODIUM
    PkgConfig::NCURSESW
)
target_link_libraries(${APP_EXECUTABLE} PRIVATE Qt6::Gui Qt6::Widgets)
# wafflehouse_sipcore is normally a static archive and therefore has no link
# step of its own. Apply PJSIP's normalized macOS framework options to the final
# application link instead of calling target_link_options() on the static target.
if(WAFFLEHOUSE_PJPROJECT_FRAMEWORK_OPTIONS)
    target_link_options(${APP_EXECUTABLE} PRIVATE
        ${WAFFLEHOUSE_PJPROJECT_FRAMEWORK_OPTIONS}
    )
endif()
if(TARGET PkgConfig::XKBCOMMON)
    target_link_libraries(${APP_EXECUTABLE} PRIVATE PkgConfig::XKBCOMMON)
endif()

# SSH's embedded interactive terminal uses forkpty(3) on Unix.  On Linux and
# the BSDs forkpty is commonly provided by libutil; macOS may expose it through
# the SDK without a separate library.  Link libutil when CMake can find it.
if(UNIX)
    find_library(WAFFLEHOUSE_UTIL_LIBRARY util)
    if(WAFFLEHOUSE_UTIL_LIBRARY)
        target_link_libraries(${APP_EXECUTABLE} PRIVATE ${WAFFLEHOUSE_UTIL_LIBRARY})
    endif()
endif()

if(APPLE)
    target_compile_definitions(${APP_EXECUTABLE} PRIVATE WAFFLEHOUSE_MACOS=1)
    set_target_properties(${APP_EXECUTABLE} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "WaffleHouse-Client"
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.wafflehouse-client"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "5.6"
        MACOSX_BUNDLE_BUNDLE_VERSION "5.6.0"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    target_compile_definitions(${APP_EXECUTABLE} PRIVATE WAFFLEHOUSE_FREEBSD=1)
elseif(WIN32)
    target_compile_definitions(${APP_EXECUTABLE} PRIVATE WAFFLEHOUSE_WINDOWS=1)
    # Keep a console subsystem so the exact same executable can run the ncurses
    # frontend with --cli. GUI mode remains available with --gui/double-click.
    set_target_properties(${APP_EXECUTABLE} PROPERTIES WIN32_EXECUTABLE FALSE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(${APP_EXECUTABLE} PRIVATE WAFFLEHOUSE_LINUX=1)
    if(WAFFLEHOUSE_TERMUX_BUILD)
        target_compile_definitions(${APP_EXECUTABLE} PRIVATE WAFFLEHOUSE_TERMUX=1)
    endif()
else()
    message(FATAL_ERROR "WaffleHouse-Client 5.6 supports Linux/Unix, FreeBSD, macOS, Windows, and Termux/Android")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(${APP_EXECUTABLE} PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(wafflehouse_sipcore PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS ${APP_EXECUTABLE}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

if(APPLE)
    # Keep built-in sounds self-contained when the .app is moved out of the source tree.
    install(DIRECTORY sounds/
        DESTINATION "${APP_EXECUTABLE}.app/Contents/Resources/sounds"
        FILES_MATCHING PATTERN "*.wav"
    )
elseif(UNIX)
    install(PROGRAMS scripts/wafflehouse-shell DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(WAFFLEHOUSE_ENABLE_SSH AND NOT ANDROID)
        install(PROGRAMS scripts/wafflehouse-ssh DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
    install(FILES data/wafflehouse-client.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    foreach(_wh_icon_size 16 22 24 32 48 64 128 256 512)
        install(FILES assets/icons/${_wh_icon_size}x${_wh_icon_size}/wafflehouse-client.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_wh_icon_size}x${_wh_icon_size}/apps)
    endforeach()
    install(DIRECTORY sounds/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/wafflehouse-client/sounds
        FILES_MATCHING PATTERN "*.wav"
    )
endif()
