#   Copyright © 2019 by The qTox Project Contributors
#
#   This file is part of qTox, a Qt-based graphical interface for Tox.
#   qTox is libre software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   qTox is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with qTox.  If not, see <http://www.gnu.org/licenses/>

################################################################################
#
# :: CMake configuration
#
################################################################################

cmake_minimum_required(VERSION 2.8.11)
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(PLATFORM_EXTENSIONS "Enable platform specific extensions, requires extra dependencies" ON)
option(UPDATE_CHECK "Enable automatic update check" ON)
option(USE_CCACHE "Use ccache when available" ON)
option(SPELL_CHECK "Enable spell cheching support" ON)
option(SVGZ_ICON "Compress the SVG icon of qTox" ON)
option(ASAN "Compile with AddressSanitizer" OFF)
option(DESKTOP_NOTIFICATIONS "Use snorenotify for desktop notifications" OFF)
option(STRICT_OPTIONS "Enable strict compile options, used by CI" OFF)

# process generated files if cmake >= 3.10
if(POLICY CMP0071)
  cmake_policy(SET CMP0071 NEW)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Options are: None, Debug, Release, RelWithDebInfo, MinSizeRel." FORCE)
endif()

if(ASAN)
    set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
    set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif()

set(ENV{PKG_CONFIG_PATH}
  ${CMAKE_SOURCE_DIR}/libs/lib/pkgconfig:/opt/ffmpeg/lib/pkgconfig:$ENV{PKG_CONFIG_PATH})

# necessary to find openal-soft on mac os
if(APPLE)
  set(ENV{PKG_CONFIG_PATH}
    /usr/local/opt/openal-soft/lib/pkgconfig:$ENV{PKG_CONFIG_PATH})
endif()

execute_process(
  COMMAND brew --prefix qt5
  OUTPUT_VARIABLE QT_PREFIX_PATH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(
  COMMAND xcode-select -p
  OUTPUT_VARIABLE CMAKE_OSX_SYSROOT
  OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_OSX_DEPLOYMENT_TARGET 10.12)
set(CMAKE_OSX_SYSROOT
  ${CMAKE_OSX_SYSROOT}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX${CMAKE_OSX_DEPLOYMENT_TARGET}.sdk)

project(qtox)

# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

set(RCC_OPTIONS -compress 9 -threshold 0)

# Use C++11.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

# Hardening flags (ASLR, warnings, etc)
set(POSITION_INDEPENDENT_CODE True)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wstrict-overflow")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wstrict-aliasing")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wold-style-cast")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-compare")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunknown-pragmas")

# Extra-strict compile options that we don't want to subject all users to by default
if (STRICT_OPTIONS)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
endif()

# avoid timestamps in binary for reproducible builds, not added until GCC 4.9
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG(-Wdate-time COMPILER_SUPPORTS_WDATE_TIME)
if (COMPILER_SUPPORTS_WDATE_TIME)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wdate-time")
endif()

if (NOT WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-all")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wstack-protector")
endif()

if (UNIX AND NOT APPLE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,now")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,relro")
endif()

include(CheckAtomic)


# Use ccache when available to speed up builds.
if (USE_CCACHE)
  find_program(CCACHE_FOUND ccache)
  if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    message(STATUS "using ccache")
  else()
    message(STATUS "ccache not found")
  endif()
else()
  message(STATUS "ccache disabled; set option USE_CCACHE=ON to use ccache if available")
endif()

# Search for headers in current directory.
include_directories(${CMAKE_BINARY_DIR})
include_directories(${CMAKE_SOURCE_DIR})

include(Dependencies)

if(NOT Qt5Widgets_VERSION VERSION_LESS "5.9")
  # Drop the file modification time of source files from generated files
  # to help with reproducible builds. We do not use QFileInfo.lastModified
  # so this has no unwanted side effects. This mtime started appearing in
  # Qt 5.8. The option to force the old file format without mtime was
  # added in Qt 5.9. See https://bugreports.qt.io/browse/QTBUG-58769
  set(RCC_OPTIONS ${RCC_OPTIONS} -format-version 1)
endif()

################################################################################
#
# :: qTox main library sources
#
################################################################################

qt5_wrap_ui(${PROJECT_NAME}_FORMS
  src/chatlog/content/filetransferwidget.ui
  src/loginscreen.ui
  src/mainwindow.ui
  src/widget/about/aboutfriendform.ui
  src/widget/form/loadhistorydialog.ui
  src/widget/form/profileform.ui
  src/widget/form/removefrienddialog.ui
  src/widget/form/searchsettingsform.ui
  src/widget/form/setpassworddialog.ui
  src/widget/form/settings/aboutsettings.ui
  src/widget/form/settings/advancedsettings.ui
  src/widget/form/settings/avform.ui
  src/widget/form/settings/generalsettings.ui
  src/widget/form/settings/privacysettings.ui
  src/widget/form/settings/userinterfacesettings.ui)

qt5_add_translation(${PROJECT_NAME}_QM_FILES
  translations/ar.ts
  translations/be.ts
  translations/bg.ts
  translations/cs.ts
  translations/da.ts
  translations/de.ts
  translations/el.ts
  translations/eo.ts
  translations/es.ts
  translations/et.ts
  translations/fa.ts
  translations/fi.ts
  translations/fr.ts
  translations/he.ts
  translations/hr.ts
  translations/hu.ts
  translations/it.ts
  translations/ja.ts
  translations/jbo.ts
  translations/ko.ts
  translations/lt.ts
  translations/mk.ts
  translations/nl.ts
  translations/no_nb.ts
  translations/pl.ts
  translations/pr.ts
  translations/pt.ts
  translations/pt_BR.ts
  translations/ro.ts
  translations/ru.ts
  translations/sk.ts
  translations/sl.ts
  translations/sr.ts
  translations/sr_Latn.ts
  translations/sv.ts
  translations/sw.ts
  translations/ta.ts
  translations/tr.ts
  translations/ug.ts
  translations/uk.ts
  translations/zh_CN.ts
  translations/zh_TW.ts
)

qt5_add_resources(
  ${PROJECT_NAME}_RESOURCES
  res.qrc
  ${CMAKE_CURRENT_BINARY_DIR}/translations.qrc
  DEPENDS ${${PROJECT_NAME}_QM_FILES}
  OPTIONS ${RCC_OPTIONS}
)

if(NOT SMILEYS)
  set(SMILEYS "")
endif()

if(NOT "${SMILEYS}" STREQUAL "DISABLED")
  qt5_add_resources(
          ${PROJECT_NAME}_RESOURCES
          smileys/emojione.qrc
          OPTIONS ${RCC_OPTIONS})

  if(NOT "${SMILEYS}" STREQUAL "MIN")
    qt5_add_resources(
            ${PROJECT_NAME}_RESOURCES
            smileys/smileys.qrc
            OPTIONS ${RCC_OPTIONS})
  endif()

endif()

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc.in"
"<!DOCTYPE RCC>
<RCC version=\"1.0\">
  <qresource prefix=\"/translations\">
")
foreach(qm ${${PROJECT_NAME}_QM_FILES})
  get_filename_component(qm_name ${qm} NAME)
  file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc.in"
    "    <file alias=\"${qm_name}\">${qm}</file>\n")
endforeach(qm)
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc.in"
"  </qresource>
</RCC>
")

execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
  ${CMAKE_CURRENT_BINARY_DIR}/translations.qrc.in
  ${CMAKE_CURRENT_BINARY_DIR}/translations.qrc)

set(${PROJECT_NAME}_SOURCES
  src/chatlog/chatlinecontent.cpp
  src/chatlog/chatlinecontent.h
  src/chatlog/chatlinecontentproxy.cpp
  src/chatlog/chatlinecontentproxy.h
  src/chatlog/chatline.cpp
  src/chatlog/chatline.h
  src/chatlog/chatlog.cpp
  src/chatlog/chatlog.h
  src/chatlog/chatmessage.cpp
  src/chatlog/chatmessage.h
  src/chatlog/content/filetransferwidget.cpp
  src/chatlog/content/filetransferwidget.h
  src/chatlog/content/image.cpp
  src/chatlog/content/image.h
  src/chatlog/content/notificationicon.cpp
  src/chatlog/content/notificationicon.h
  src/chatlog/content/spinner.cpp
  src/chatlog/content/spinner.h
  src/chatlog/content/text.cpp
  src/chatlog/content/text.h
  src/chatlog/content/timestamp.cpp
  src/chatlog/content/timestamp.h
  src/chatlog/content/broken.cpp
  src/chatlog/content/broken.h
  src/chatlog/customtextdocument.cpp
  src/chatlog/customtextdocument.h
  src/chatlog/documentcache.cpp
  src/chatlog/documentcache.h
  src/chatlog/pixmapcache.cpp
  src/chatlog/pixmapcache.h
  src/chatlog/toxfileprogress.cpp
  src/chatlog/toxfileprogress.h
  src/chatlog/textformatter.cpp
  src/chatlog/textformatter.h
  src/core/coreav.cpp
  src/core/coreav.h
  src/core/core.cpp
  src/core/corefile.cpp
  src/core/corefile.h
  src/core/core.h
  src/core/dhtserver.cpp
  src/core/dhtserver.h
  src/core/icoresettings.h
  src/core/toxcall.cpp
  src/core/toxcall.h
  src/core/toxencrypt.cpp
  src/core/toxencrypt.h
  src/core/toxfile.cpp
  src/core/toxfile.h
  src/core/toxfilepause.h
  src/core/toxid.cpp
  src/core/toxid.h
  src/core/groupid.cpp
  src/core/groupid.h
  src/core/toxlogger.cpp
  src/core/toxlogger.h
  src/core/toxoptions.cpp
  src/core/toxoptions.h
  src/core/toxpk.cpp
  src/core/toxpk.h
  src/core/contactid.cpp
  src/core/contactid.h
  src/core/toxstring.cpp
  src/core/toxstring.h
  src/friendlist.cpp
  src/friendlist.h
  src/grouplist.cpp
  src/grouplist.h
  src/ipc.cpp
  src/ipc.h
  src/model/about/aboutfriend.cpp
  src/model/about/aboutfriend.h
  src/model/about/iaboutfriend.h
  src/model/chatroom/chatroom.h
  src/model/chatroom/friendchatroom.cpp
  src/model/chatroom/friendchatroom.h
  src/model/chatroom/groupchatroom.cpp
  src/model/chatroom/groupchatroom.h
  src/model/contact.cpp
  src/model/contact.h
  src/model/chatlogitem.cpp
  src/model/chatlogitem.h
  src/model/exiftransform.h
  src/model/exiftransform.cpp
  src/model/friend.cpp
  src/model/friend.h
  src/model/message.h
  src/model/message.cpp
  src/model/imessagedispatcher.h
  src/model/friendmessagedispatcher.h
  src/model/friendmessagedispatcher.cpp
  src/model/groupmessagedispatcher.h
  src/model/groupmessagedispatcher.cpp
  src/model/message.h
  src/model/message.cpp
  src/model/groupinvite.cpp
  src/model/groupinvite.h
  src/model/group.cpp
  src/model/group.h
  src/model/status.cpp
  src/model/status.h
  src/model/profile/iprofileinfo.h
  src/model/profile/profileinfo.cpp
  src/model/profile/profileinfo.h
  src/model/dialogs/idialogs.h
  src/model/ichatlog.h
  src/model/sessionchatlog.h
  src/model/sessionchatlog.cpp
  src/model/chathistory.h
  src/model/chathistory.cpp
  src/model/toxclientstandards.h
  src/model/ibootstraplistgenerator.h
  src/net/bootstrapnodeupdater.cpp
  src/net/bootstrapnodeupdater.h
  src/net/avatarbroadcaster.cpp
  src/net/avatarbroadcaster.h
  src/net/toxuri.cpp
  src/net/toxuri.h
  src/nexus.cpp
  src/nexus.h
  src/persistence/db/rawdatabase.cpp
  src/persistence/db/rawdatabase.h
  src/persistence/history.cpp
  src/persistence/history.h
  src/persistence/ifriendsettings.h
  src/persistence/offlinemsgengine.cpp
  src/persistence/offlinemsgengine.h
  src/persistence/paths.cpp
  src/persistence/paths.h
  src/persistence/profile.cpp
  src/persistence/profile.h
  src/persistence/profilelocker.cpp
  src/persistence/profilelocker.h
  src/persistence/serialize.cpp
  src/persistence/serialize.h
  src/persistence/settings.cpp
  src/persistence/settings.h
  src/persistence/settingsserializer.cpp
  src/persistence/settingsserializer.h
  src/persistence/smileypack.cpp
  src/persistence/smileypack.h
  src/persistence/toxsave.cpp
  src/persistence/toxsave.h
  src/video/cameradevice.cpp
  src/video/cameradevice.h
  src/video/camerasource.cpp
  src/video/camerasource.h
  src/video/corevideosource.cpp
  src/video/corevideosource.h
  src/video/ivideosettings.h
  src/video/netcamview.cpp
  src/video/netcamview.h
  src/video/videoframe.cpp
  src/video/videoframe.h
  src/video/videomode.cpp
  src/video/videomode.h
  src/video/videosource.cpp
  src/video/videosource.h
  src/video/videosurface.cpp
  src/video/videosurface.h
  src/widget/about/aboutfriendform.cpp
  src/widget/about/aboutfriendform.h
  src/widget/categorywidget.cpp
  src/widget/categorywidget.h
  src/widget/chatformheader.cpp
  src/widget/chatformheader.h
  src/widget/circlewidget.cpp
  src/widget/circlewidget.h
  src/widget/contentdialog.cpp
  src/widget/contentdialog.h
  src/widget/contentdialogmanager.cpp
  src/widget/contentdialogmanager.h
  src/widget/contentlayout.cpp
  src/widget/contentlayout.h
  src/widget/emoticonswidget.cpp
  src/widget/emoticonswidget.h
  src/widget/flowlayout.cpp
  src/widget/flowlayout.h
  src/widget/searchform.cpp
  src/widget/searchform.h
  src/widget/searchtypes.h
  src/widget/form/addfriendform.cpp
  src/widget/form/addfriendform.h
  src/widget/form/chatform.cpp
  src/widget/form/chatform.h
  src/widget/form/filesform.cpp
  src/widget/form/filesform.h
  src/widget/form/genericchatform.cpp
  src/widget/form/genericchatform.h
  src/widget/form/groupchatform.cpp
  src/widget/form/groupchatform.h
  src/widget/form/groupinviteform.cpp
  src/widget/form/groupinviteform.h
  src/widget/form/groupinvitewidget.cpp
  src/widget/form/groupinvitewidget.h
  src/widget/form/loadhistorydialog.cpp
  src/widget/form/loadhistorydialog.h
  src/widget/form/profileform.cpp
  src/widget/form/profileform.h
  src/widget/form/searchsettingsform.cpp
  src/widget/form/searchsettingsform.h
  src/widget/form/setpassworddialog.cpp
  src/widget/form/setpassworddialog.h
  src/widget/form/settings/aboutform.cpp
  src/widget/form/settings/aboutform.h
  src/widget/form/settings/advancedform.cpp
  src/widget/form/settings/advancedform.h
  src/widget/form/settings/avform.cpp
  src/widget/form/settings/avform.h
  src/widget/form/settings/generalform.cpp
  src/widget/form/settings/generalform.h
  src/widget/form/settings/genericsettings.cpp
  src/widget/form/settings/genericsettings.h
  src/widget/form/settings/privacyform.cpp
  src/widget/form/settings/privacyform.h
  src/widget/form/settings/userinterfaceform.h
  src/widget/form/settings/userinterfaceform.cpp
  src/widget/form/settings/verticalonlyscroller.cpp
  src/widget/form/settings/verticalonlyscroller.h
  src/widget/form/settingswidget.cpp
  src/widget/form/settingswidget.h
  src/widget/form/tabcompleter.cpp
  src/widget/form/tabcompleter.h
  src/widget/friendlistlayout.cpp
  src/widget/friendlistlayout.h
  src/widget/friendlistwidget.cpp
  src/widget/friendlistwidget.h
  src/widget/friendwidget.cpp
  src/widget/friendwidget.h
  src/widget/genericchatitemlayout.cpp
  src/widget/genericchatitemlayout.h
  src/widget/genericchatitemwidget.cpp
  src/widget/genericchatitemwidget.h
  src/widget/genericchatroomwidget.cpp
  src/widget/genericchatroomwidget.h
  src/widget/groupwidget.cpp
  src/widget/groupwidget.h
  src/widget/gui.cpp
  src/widget/gui.h
  src/widget/loginscreen.cpp
  src/widget/loginscreen.h
  src/widget/maskablepixmapwidget.cpp
  src/widget/maskablepixmapwidget.h
  src/widget/notificationedgewidget.cpp
  src/widget/notificationedgewidget.h
  src/widget/notificationscrollarea.cpp
  src/widget/notificationscrollarea.h
  src/widget/passwordedit.cpp
  src/widget/passwordedit.h
  src/widget/qrwidget.cpp
  src/widget/qrwidget.h
  src/widget/splitterrestorer.cpp
  src/widget/splitterrestorer.h
  src/widget/style.cpp
  src/widget/style.h
  src/widget/tool/activatedialog.cpp
  src/widget/tool/activatedialog.h
  src/widget/tool/adjustingscrollarea.cpp
  src/widget/tool/adjustingscrollarea.h
  src/widget/tool/callconfirmwidget.cpp
  src/widget/tool/callconfirmwidget.h
  src/widget/tool/chattextedit.cpp
  src/widget/tool/chattextedit.h
  src/widget/tool/croppinglabel.cpp
  src/widget/tool/croppinglabel.h
  src/widget/tool/flyoutoverlaywidget.cpp
  src/widget/tool/flyoutoverlaywidget.h
  src/widget/tool/friendrequestdialog.cpp
  src/widget/tool/friendrequestdialog.h
  src/widget/tool/identicon.cpp
  src/widget/tool/identicon.h
  src/widget/tool/movablewidget.cpp
  src/widget/tool/movablewidget.h
  src/widget/tool/profileimporter.cpp
  src/widget/tool/profileimporter.h
  src/widget/tool/recursivesignalblocker.cpp
  src/widget/tool/recursivesignalblocker.h
  src/widget/tool/removefrienddialog.cpp
  src/widget/tool/removefrienddialog.h
  src/widget/tool/screengrabberchooserrectitem.cpp
  src/widget/tool/screengrabberchooserrectitem.h
  src/widget/tool/screengrabberoverlayitem.cpp
  src/widget/tool/screengrabberoverlayitem.h
  src/widget/tool/screenshotgrabber.cpp
  src/widget/tool/screenshotgrabber.h
  src/widget/tool/toolboxgraphicsitem.cpp
  src/widget/tool/toolboxgraphicsitem.h
  src/widget/translator.cpp
  src/widget/translator.h
  src/widget/widget.cpp
  src/widget/widget.h
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  set(${PROJECT_NAME}_SOURCES ${${PROJECT_NAME}_SOURCES}
    src/platform/camera/directshow.cpp
    src/platform/camera/directshow.h
  )
  set(${PROJECT_NAME}_RESOURCES ${${PROJECT_NAME}_RESOURCES}
    windows/qtox.rc
  )
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux" OR ${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
  set(${PROJECT_NAME}_SOURCES ${${PROJECT_NAME}_SOURCES}
    src/platform/camera/v4l2.cpp
    src/platform/camera/v4l2.h
  )
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(${PROJECT_NAME}_SOURCES ${${PROJECT_NAME}_SOURCES}
    src/platform/install_osx.cpp
    src/platform/install_osx.h
  )
endif()

if (UNIX)
  set(${PROJECT_NAME}_SOURCES ${${PROJECT_NAME}_SOURCES}
    src/platform/posixsignalnotifier.cpp
    src/platform/posixsignalnotifier.h
  )
endif()

if (PLATFORM_EXTENSIONS)
  set(${PROJECT_NAME}_SOURCES ${${PROJECT_NAME}_SOURCES}
    src/platform/autorun.h
    src/platform/capslock.h
    src/platform/timer.h
  )
  if (WIN32)
    set(${PROJECT_NAME}_SOURCES ${${PROJECT_NAME}_SOURCES}
      src/platform/autorun_win.cpp
      src/platform/capslock_win.cpp
      src/platform/timer_win.cpp
    )
  elseif (${X11_EXT})
    set(${PROJECT_NAME}_SOURCES ${${PROJECT_NAME}_SOURCES}
      src/platform/autorun_xdg.cpp
      src/platform/capslock_x11.cpp
      src/platform/timer_x11.cpp
      src/platform/x11_display.cpp
    )
  elseif (${APPLE_EXT})
    set(${PROJECT_NAME}_SOURCES ${${PROJECT_NAME}_SOURCES}
      src/platform/autorun_osx.cpp
      src/platform/capslock_osx.cpp
      src/platform/timer_osx.cpp
    )
  endif()
endif()

add_definitions(-DQT_MESSAGELOGCONTEXT=1)

if(AVFOUNDATION_FOUND)
  set(${PROJECT_NAME}_SOURCES ${${PROJECT_NAME}_SOURCES}
    src/platform/camera/avfoundation.mm
    src/platform/camera/avfoundation.h)
endif()

if(${UPDATE_CHECK})
    add_definitions(-DUPDATE_CHECK_ENABLED=1)
    set(${PROJECT_NAME}_SOURCES ${${PROJECT_NAME}_SOURCES}
        src/net/updatecheck.cpp
        src/net/updatecheck.h)
    message(STATUS "using update check")
else()
    message(STATUS "NOT using update check")
endif()

if (${DESKTOP_NOTIFICATIONS})
    add_definitions(-DDESKTOP_NOTIFICATIONS=1)
    set(${PROJECT_NAME}_SOURCES ${${PROJECT_NAME}_SOURCES}
        src/platform/desktop_notifications/desktopnotify.cpp
        src/platform/desktop_notifications/desktopnotify.h)
    message(STATUS "using desktop notifications")
else()
    add_definitions(-DDESKTOP_NOTIFICATIONS=0)
    message(STATUS "not using desktop notifications")
endif()

if (MINGW)
  STRING(TOLOWER "${CMAKE_BUILD_TYPE}" CMAKE_BUILD_TYPE_LOWER)
  if (CMAKE_BUILD_TYPE_LOWER MATCHES debug)
    # Allows wine to display source code file names and line numbers on crash in its backtrace
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gdwarf-2")
  endif()
endif()

# the compiler flags for compiling C sources
MESSAGE( STATUS "CMAKE_C_FLAGS: " ${CMAKE_C_FLAGS} )

# the compiler flags for compiling C++ sources
MESSAGE( STATUS "CMAKE_CXX_FLAGS: " ${CMAKE_CXX_FLAGS} )

add_subdirectory(util)
add_subdirectory(audio)

add_library(${PROJECT_NAME}_static
  STATIC
  ${${PROJECT_NAME}_FORMS}
  ${${PROJECT_NAME}_SOURCES}
  ${${PROJECT_NAME}_QM_FILES}
  ${${PROJECT_NAME}_RESOURCES})
target_link_libraries(${PROJECT_NAME}_static
  ${CMAKE_REQUIRED_LIBRARIES}
  ${ALL_LIBRARIES})

target_link_libraries(${PROJECT_NAME}_static util_library)
target_link_libraries(${PROJECT_NAME}_static audio_library)

add_executable(${PROJECT_NAME}
  WIN32
  MACOSX_BUNDLE
  ${${PROJECT_NAME}_RESOURCES}
  src/main.cpp)
target_link_libraries(${PROJECT_NAME}
  ${PROJECT_NAME}_static
  ${CMAKE_REQUIRED_LIBRARIES}
  ${ALL_LIBRARIES})

include(Testing)
include(Installation)

if (DEFINED ENV{IN_NIX_SHELL})
  # the qtox binary must be "wrapped" to find the Qt platform plugin
  # and other dependencies at runtime
  add_custom_command(
    TARGET ${PROJECT_NAME}
    POST_BUILD
    COMMAND nix-shell --run "wrapQtApp ${PROJECT_NAME}")
endif()
