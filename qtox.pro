#-------------------------------------------------
#
# Project created by QtCreator 2014-06-22T14:07:35
#
#-------------------------------------------------

#    This file is part of qTox, a Qt-based graphical interface for Tox.
#
#    This program is libre software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
#    See the COPYING file for more details.


message()
message("Warning: This project file is deprecated and should not be used anymore except on FreeBSD.")
message("Use the CMakeLists.txt file instead to open this as a CMake project.")
message()

unix:!freebsd {
    error("qmake builds are not supported on this platform anymore")
}


QT       += core gui network xml opengl svg widgets

TARGET    = qtox
TEMPLATE  = app
FORMS    += \
    src/loginscreen.ui \
    src/mainwindow.ui \
    src/chatlog/content/filetransferwidget.ui \
    src/widget/form/profileform.ui \
    src/widget/form/loadhistorydialog.ui \
    src/widget/form/setpassworddialog.ui \
    src/widget/form/settings/generalsettings.ui \
    src/widget/form/settings/userinterfacesettings.ui \
    src/widget/form/settings/privacysettings.ui \
    src/widget/form/settings/avform.ui \
    src/widget/form/settings/advancedsettings.ui \
    src/widget/form/settings/aboutsettings.ui \
    src/widget/form/removefrienddialog.ui \
    src/widget/about/aboutfriendform.ui

CONFIG   += c++11
CONFIG   += warn_on exceptions_off rtti_off
CONFIG   += link_pkgconfig
# undocumented, but just works™
CONFIG   += silent


# Hardening flags (ASLR, warnings, etc)
# TODO: add `-Werror` to hardening flags once all warnings are fixed
QMAKE_CXXFLAGS += -fPIE \
                  -Wstrict-overflow \
                  -Wstrict-aliasing

!win32 {
    QMAKE_LFLAGS   += -pie
    QMAKE_CXXFLAGS += -fstack-protector-all \
                      -Wstack-protector
}

# osx & windows cannot into security (build on it fails with those enabled)
unix:!macx {
    QMAKE_LFLAGS += -Wl,-z,now -Wl,-z,relro
}

# needed, since `rtti_off` doesn't work
QMAKE_CXXFLAGS += -fno-rtti
QMAKE_RESOURCE_FLAGS += -compress 9 -threshold 0

# Rules for creating/updating {ts|qm}-files
include(translations/i18n.pri)
# Build all the qm files now, to make RCC happy
system($$fromfile(translations/i18n.pri, updateallqm))

isEmpty(GIT_VERSION) {
    GIT_VERSION = $$system(git rev-parse HEAD 2> /dev/null || echo "built without git")
}
DEFINES += GIT_VERSION=\"\\\"$$quote($$GIT_VERSION)\\\"\"
isEmpty(GIT_DESCRIBE) {
    GIT_DESCRIBE = $$system(git describe --tags 2> /dev/null || echo "Nightly")
}
DEFINES += GIT_DESCRIBE=\"\\\"$$quote($$GIT_DESCRIBE)\\\"\"
# date works on linux/mac, but it would hangs qmake on windows
# This hack returns 0 on batch (windows), but executes "date +%s" or return 0 if it fails on bash (linux/mac)
TIMESTAMP = $$system($1 2>null||echo 0||a;rm null;date +%s||echo 0) # I'm so sorry
DEFINES += TIMESTAMP=$$TIMESTAMP
DEFINES += LOG_TO_FILE
DEFINES += QT_MESSAGELOGCONTEXT

contains(DISABLE_PLATFORM_EXT, YES) {

} else {
    DEFINES += QTOX_PLATFORM_EXT
}

contains(JENKINS,YES) {
    INCLUDEPATH += ./libs/include/
    TOX_CMAKE = YES
} else {
    INCLUDEPATH += libs/include
}

contains(DEFINES, QTOX_PLATFORM_EXT) {
    HEADERS += src/platform/timer.h
    SOURCES += src/platform/timer_osx.cpp \
               src/platform/timer_win.cpp \
               src/platform/timer_x11.cpp

    HEADERS += src/platform/autorun.h
    SOURCES += src/platform/autorun_win.cpp \
               src/platform/autorun_xdg.cpp \
               src/platform/autorun_osx.cpp

    HEADERS += src/platform/capslock.h
    SOURCES += src/platform/capslock_win.cpp \
               src/platform/capslock_x11.cpp \
               src/platform/capslock_osx.cpp

    HEADERS += src/platform/x11_display.h
    SOURCES += src/platform/x11_display.cpp
}

# Rules for Windows, Mac OSX, and Linux
win32 {
    # windows-specific hardening (ASLR, DEP protection)
    QMAKE_LFLAGS += -Wl,--dynamicbase -Wl,--nxcompat

    RC_FILE = windows/qtox.rc
    LIBS += -L$$PWD/libs/lib \
            -ltoxav \
            -ltoxcore \
            -ltoxencryptsave

    # must be exactly here, to preserve link order
    contains(TOX_CMAKE, YES) {
        LIBS += -ltoxgroup \
                -ltoxmessenger \
                -ltoxfriends \
                -ltoxnetcrypto \
                -ltoxdht \
                -ltoxnetwork \
                -ltoxcrypto
    }

    LIBS += -lsodium \
            -lvpx \
            -lpthread \
            -lavdevice \
            -lavformat \
            -lavcodec \
            -lavutil \
            -lexif \
            -lswscale \
            -lOpenAL32 \
            -lopus \
            -lqrencode \
            -lsqlcipher \
            -lcrypto \
            -lopengl32 \
            -lole32 \
            -loleaut32 \
            -lvfw32 \
            -lws2_32 \
            -liphlpapi \
            -lgdi32 \
            -lshlwapi \
            -luuid
    LIBS += -lstrmiids # For DirectShow
} else {
    isEmpty(PREFIX) {
        PREFIX = /usr
    }

    BINDIR = $$PREFIX/bin
    DATADIR = $$PREFIX/share
    target.path = $$BINDIR
    desktop.path = $$DATADIR/applications
    desktop.files += qtox.desktop
    appdata.path = $$DATADIR/appdata
    appdata.files += res/qTox.appdata.xml
    INSTALLS += target desktop appdata

    # Install application icons according to the XDG spec
    ICON_SIZES = 14 16 22 24 32 36 48 64 72 96 128 192 256 512
    for(icon_size, ICON_SIZES) {
        icon_$${icon_size}.files = img/icons/$${icon_size}x$${icon_size}/qtox.png
        icon_$${icon_size}.path = $$DATADIR/icons/hicolor/$${icon_size}x$${icon_size}/apps
        INSTALLS += icon_$${icon_size}
    }
    icon_scalable.files = img/icons/qtox.svg
    icon_scalable.path = $$DATADIR/icons/hicolor/scalable/apps
    INSTALLS += icon_scalable

    # If we're building a package, static link libtox[core,av] and
    # libsodium, since they are not provided by any package
    contains(STATICPKG, YES) {
        LIBS += -L$$PWD/libs/lib/ \
                -lopus \
                -lvpx \
                -lopenal \
                -Wl,-Bstatic \
                -ltoxcore \
                -ltoxav \
                -ltoxencryptsave \
                -lsodium \
                -lavformat \
                -lavdevice \
                -lavcodec \
                -lavutil \
                -lexif \
                -lswscale \
                -lz \
                -ljpeg \
                -ltiff \
                -lpng \
                -ljasper \
                -lIlmImf \
                -lIlmThread \
                -lIex \
                -ldc1394 \
                -lraw1394 \
                -lHalf \
                -llzma \
                -ljbig \
                -Wl,-Bdynamic \
                -lv4l1 \
                -lv4l2 \
                -lavformat \
                -lavcodec \
                -lavutil \
                -lswscale \
                -lusb-1.0 \
                -lqrencode \
                -lsqlcipher
    } else {
        LIBS += -L$$PWD/libs/lib/ \
                -ltoxcore \
                -ltoxav \
                -ltoxencryptsave \
                -lvpx \
                -lsodium \
                -lopenal \
                -lavformat \
                -lavdevice \
                -lavcodec \
                -lavutil \
                -lexif \
                -lswscale \
                -lqrencode \
                -lsqlcipher \
                -lfilteraudio
    }

    contains(DEFINES, QTOX_PLATFORM_EXT) {
        LIBS += -lX11 \
                -lXss
    }
}

unix:!macx {
    # The systray Unity backend implements the system tray icon on Unity (Ubuntu) and GNOME desktops.
    contains(ENABLE_SYSTRAY_UNITY_BACKEND, YES) {
        DEFINES += ENABLE_SYSTRAY_UNITY_BACKEND

        PKGCONFIG += glib-2.0 gtk+-2.0 atk
        PKGCONFIG += cairo gdk-pixbuf-2.0 pango
        PKGCONFIG += appindicator-0.1 dbusmenu-glib-0.4
    }

    # The systray Status Notifier backend implements the system tray icon on KDE and compatible desktops
    !contains(ENABLE_SYSTRAY_STATUSNOTIFIER_BACKEND, NO) {
        DEFINES += ENABLE_SYSTRAY_STATUSNOTIFIER_BACKEND

        PKGCONFIG += glib-2.0 gtk+-2.0 atk
        PKGCONFIG += cairo gdk-pixbuf-2.0 pango

        SOURCES +=     src/platform/statusnotifier/closures.c \
        src/platform/statusnotifier/enums.c \
        src/platform/statusnotifier/statusnotifier.c

        HEADERS += src/platform/statusnotifier/closures.h \
        src/platform/statusnotifier/enums.h \
        src/platform/statusnotifier/interfaces.h \
        src/platform/statusnotifier/statusnotifier.h
    }

    # The systray GTK backend implements a system tray icon compatible with many systems
    !contains(ENABLE_SYSTRAY_GTK_BACKEND, NO) {
        DEFINES += ENABLE_SYSTRAY_GTK_BACKEND

        PKGCONFIG += glib-2.0 gtk+-2.0 atk
        PKGCONFIG += gdk-pixbuf-2.0 cairo pango
    }

    # ffmpeg
    PKGCONFIG += libavformat libavdevice libavcodec
    PKGCONFIG += libavutil libswscale
}

win32 {
    HEADERS += \
        src/platform/camera/directshow.h

    SOURCES += \
        src/platform/camera/directshow.cpp
}

freebsd {
    HEADERS += \
        src/platform/camera/v4l2.h

    SOURCES += \
        src/platform/camera/v4l2.cpp

    desktop.files = qtox.desktop

    icon.files = img/qtox.png
    icon.path = $$PREFIX/share/pixmaps

    INSTALLS = target desktop icon
}

RESOURCES += res.qrc \
             translations/translations.qrc

!contains(SMILEYS, DISABLED) {
    RESOURCES += smileys/emojione.qrc
    !contains(SMILEYS, MIN) {
        RESOURCES += smileys/smileys.qrc
    }
}

HEADERS  += \
    src/audio/audio.h \
    src/audio/backend/openal.h \
    src/audio/backend/openal2.h \
    src/chatlog/chatline.h \
    src/chatlog/chatlinecontent.h \
    src/chatlog/chatlinecontentproxy.h \
    src/chatlog/chatlog.h \
    src/chatlog/chatmessage.h \
    src/chatlog/content/filetransferwidget.h \
    src/chatlog/content/image.h \
    src/chatlog/content/notificationicon.h \
    src/chatlog/content/spinner.h \
    src/chatlog/content/text.h \
    src/chatlog/content/timestamp.h \
    src/chatlog/customtextdocument.h \
    src/chatlog/documentcache.h \
    src/chatlog/pixmapcache.h \
    src/chatlog/textformatter.h \
    src/core/core.h \
    src/core/coreav.h \
    src/core/corefile.h \
    src/core/corestructs.h \
    src/core/indexedlist.h \
    src/core/recursivesignalblocker.h \
    src/core/toxcall.h \
    src/core/toxencrypt.h \
    src/core/toxid.h \
    src/core/toxpk.h \
    src/core/toxstring.h \
    src/friendlist.h \
    src/groupinvite.h \
    src/grouplist.h \
    src/ipc.h \
    src/model/about/aboutfriend.h \
    src/model/about/iaboutfriend.h \
    src/model/contact.h \
    src/model/friend.h \
    src/model/group.h \
    src/net/autoupdate.h \
    src/net/avatarbroadcaster.h \
    src/net/toxme.h \
    src/net/toxuri.h \
    src/nexus.h \
    src/persistence/db/rawdatabase.h \
    src/persistence/history.h \
    src/persistence/offlinemsgengine.h \
    src/persistence/profile.h \
    src/persistence/profilelocker.h \
    src/persistence/serialize.h \
    src/persistence/settings.h \
    src/persistence/settingsserializer.h \
    src/persistence/smileypack.h \
    src/persistence/toxsave.h \
    src/video/cameradevice.h \
    src/video/camerasource.h \
    src/video/corevideosource.h \
    src/video/genericnetcamview.h \
    src/video/groupnetcamview.h \
    src/video/netcamview.h \
    src/video/videoframe.h \
    src/video/videomode.h \
    src/video/videosource.h \
    src/video/videosurface.h \
    src/widget/about/aboutfriendform.h \
    src/widget/categorywidget.h \
    src/widget/circlewidget.h \
    src/widget/contentdialog.h \
    src/widget/contentlayout.h \
    src/widget/emoticonswidget.h \
    src/widget/form/addfriendform.h \
    src/widget/form/chatform.h \
    src/widget/form/filesform.h \
    src/widget/form/genericchatform.h \
    src/widget/form/groupchatform.h \
    src/widget/form/groupinviteform.h \
    src/widget/form/groupinvitewidget.h \
    src/widget/form/loadhistorydialog.h \
    src/widget/form/profileform.h \
    src/widget/form/setpassworddialog.h \
    src/widget/form/settings/aboutform.h \
    src/widget/form/settings/advancedform.h \
    src/widget/form/settings/avform.h \
    src/widget/form/settings/generalform.h \
    src/widget/form/settings/genericsettings.h \
    src/widget/form/settings/privacyform.h \
    src/widget/form/settings/userinterfaceform.h \
    src/widget/form/settings/verticalonlyscroller.h \
    src/widget/form/settingswidget.h \
    src/widget/form/tabcompleter.h \
    src/widget/friendlistlayout.h \
    src/widget/friendlistwidget.h \
    src/widget/friendwidget.h \
    src/widget/genericchatitemlayout.h \
    src/widget/genericchatitemwidget.h \
    src/widget/genericchatroomwidget.h \
    src/widget/groupwidget.h \
    src/widget/gui.h \
    src/widget/loginscreen.h \
    src/widget/maskablepixmapwidget.h \
    src/widget/notificationedgewidget.h \
    src/widget/notificationscrollarea.h \
    src/widget/passwordedit.h \
    src/widget/qrwidget.h \
    src/widget/splitterrestorer.h \
    src/widget/style.h \
    src/widget/systemtrayicon.h \
    src/widget/systemtrayicon_private.h \
    src/widget/tool/activatedialog.h \
    src/widget/tool/adjustingscrollarea.h \
    src/widget/tool/callconfirmwidget.h \
    src/widget/tool/chattextedit.h \
    src/widget/tool/croppinglabel.h \
    src/widget/tool/flyoutoverlaywidget.h \
    src/widget/tool/friendrequestdialog.h \
    src/widget/tool/movablewidget.h \
    src/widget/tool/profileimporter.h \
    src/widget/tool/removefrienddialog.h \
    src/widget/tool/screengrabberchooserrectitem.h \
    src/widget/tool/screengrabberoverlayitem.h \
    src/widget/tool/screenshotgrabber.h \
    src/widget/tool/toolboxgraphicsitem.h \
    src/widget/translator.h \
    src/widget/widget.h

SOURCES += \
    src/audio/audio.cpp \
    src/audio/backend/openal.cpp \
    src/audio/backend/openal2.cpp \
    src/chatlog/chatline.cpp \
    src/chatlog/chatlinecontent.cpp \
    src/chatlog/chatlinecontentproxy.cpp \
    src/chatlog/chatlog.cpp \
    src/chatlog/chatmessage.cpp \
    src/chatlog/content/filetransferwidget.cpp \
    src/chatlog/content/image.cpp \
    src/chatlog/content/notificationicon.cpp \
    src/chatlog/content/spinner.cpp \
    src/chatlog/content/text.cpp \
    src/chatlog/content/timestamp.cpp \
    src/chatlog/customtextdocument.cpp\
    src/chatlog/documentcache.cpp \
    src/chatlog/pixmapcache.cpp \
    src/chatlog/textformatter.cpp \
    src/core/core.cpp \
    src/core/coreav.cpp \
    src/core/corefile.cpp \
    src/core/corestructs.cpp \
    src/core/recursivesignalblocker.cpp \
    src/core/toxcall.cpp \
    src/core/toxencrypt.cpp \
    src/core/toxid.cpp \
    src/core/toxpk.cpp \
    src/core/toxstring.cpp \
    src/friendlist.cpp \
    src/groupinvite.cpp \
    src/grouplist.cpp \
    src/ipc.cpp \
    src/main.cpp \
    src/model/about/aboutfriend.cpp \
    src/model/contact.cpp \
    src/model/friend.cpp \
    src/model/group.cpp \
    src/net/autoupdate.cpp \
    src/net/avatarbroadcaster.cpp \
    src/net/toxme.cpp \
    src/net/toxuri.cpp \
    src/nexus.cpp \
    src/persistence/db/rawdatabase.cpp \
    src/persistence/history.cpp \
    src/persistence/offlinemsgengine.cpp \
    src/persistence/profile.cpp \
    src/persistence/profilelocker.cpp \
    src/persistence/serialize.cpp \
    src/persistence/settings.cpp \
    src/persistence/settingsserializer.cpp \
    src/persistence/smileypack.cpp \
    src/persistence/toxsave.cpp \
    src/video/cameradevice.cpp \
    src/video/camerasource.cpp \
    src/video/corevideosource.cpp \
    src/video/genericnetcamview.cpp \
    src/video/groupnetcamview.cpp \
    src/video/netcamview.cpp \
    src/video/videoframe.cpp \
    src/video/videomode.cpp \
    src/video/videosource.cpp \
    src/video/videosurface.cpp \
    src/widget/about/aboutfriendform.cpp \
    src/widget/categorywidget.cpp \
    src/widget/circlewidget.cpp \
    src/widget/contentdialog.cpp \
    src/widget/contentlayout.cpp \
    src/widget/emoticonswidget.cpp \
    src/widget/flowlayout.cpp \
    src/widget/form/addfriendform.cpp \
    src/widget/form/chatform.cpp \
    src/widget/form/filesform.cpp \
    src/widget/form/genericchatform.cpp \
    src/widget/form/groupchatform.cpp \
    src/widget/form/groupinviteform.cpp \
    src/widget/form/groupinvitewidget.cpp \
    src/widget/form/loadhistorydialog.cpp \
    src/widget/form/profileform.cpp \
    src/widget/form/setpassworddialog.cpp \
    src/widget/form/settings/aboutform.cpp \
    src/widget/form/settings/advancedform.cpp \
    src/widget/form/settings/avform.cpp \
    src/widget/form/settings/generalform.cpp \
    src/widget/form/settings/genericsettings.cpp \
    src/widget/form/settings/privacyform.cpp \
    src/widget/form/settings/userinterfaceform.cpp \
    src/widget/form/settings/verticalonlyscroller.cpp \
    src/widget/form/settingswidget.cpp \
    src/widget/form/tabcompleter.cpp \
    src/widget/friendlistlayout.cpp \
    src/widget/friendlistwidget.cpp \
    src/widget/friendwidget.cpp \
    src/widget/genericchatitemlayout.cpp \
    src/widget/genericchatitemwidget.cpp \
    src/widget/genericchatroomwidget.cpp \
    src/widget/groupwidget.cpp \
    src/widget/gui.cpp \
    src/widget/loginscreen.cpp \
    src/widget/maskablepixmapwidget.cpp \
    src/widget/notificationedgewidget.cpp \
    src/widget/notificationscrollarea.cpp \
    src/widget/passwordedit.cpp \
    src/widget/qrwidget.cpp \
    src/widget/splitterrestorer.cpp \
    src/widget/style.cpp \
    src/widget/systemtrayicon.cpp \
    src/widget/tool/activatedialog.cpp \
    src/widget/tool/adjustingscrollarea.cpp \
    src/widget/tool/callconfirmwidget.cpp \
    src/widget/tool/chattextedit.cpp \
    src/widget/tool/croppinglabel.cpp \
    src/widget/tool/flyoutoverlaywidget.cpp \
    src/widget/tool/friendrequestdialog.cpp \
    src/widget/tool/movablewidget.cpp \
    src/widget/tool/profileimporter.cpp \
    src/widget/tool/removefrienddialog.cpp \
    src/widget/tool/screengrabberchooserrectitem.cpp \
    src/widget/tool/screengrabberoverlayitem.cpp \
    src/widget/tool/screenshotgrabber.cpp \
    src/widget/tool/toolboxgraphicsitem.cpp \
    src/widget/translator.cpp \
    src/widget/widget.cpp
