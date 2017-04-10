/*
    Copyright © 2014-2015 by The qTox Project Contributors

    This file is part of qTox, a Qt-based graphical interface for Tox.

    qTox is libre software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    qTox is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with qTox.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "persistence/settings.h"
#include "src/ipc.h"
#include "src/net/autoupdate.h"
#include "src/net/toxuri.h"
#include "src/nexus.h"
#include "src/persistence/profile.h"
#include "src/persistence/toxsave.h"
#include "src/video/camerasource.h"
#include "src/widget/loginscreen.h"
#include "src/widget/translator.h"
#include "widget/widget.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QMutex>
#include <QMutexLocker>

#include <sodium.h>
#include <stdio.h>
#include <time.h>

#if defined(Q_OS_OSX)
#include "platform/install_osx.h"
#endif

#ifdef LOG_TO_FILE
static QAtomicPointer<FILE> logFileFile = nullptr;
static QList<QByteArray>* logBuffer =
    new QList<QByteArray>(); // Store log messages until log file opened
QMutex* logBufferMutex = new QMutex();
#endif

void logMessageHandler(QtMsgType type, const QMessageLogContext& ctxt, const QString& msg)
{
    // Silence qWarning spam due to bug in QTextBrowser (trying to open a file for base64 images)
    if (ctxt.function == QString("virtual bool QFSFileEngine::open(QIODevice::OpenMode)")
        && msg == QString("QFSFileEngine::open: No file name specified"))
        return;

    QString file = ctxt.file;
    // We're not using QT_MESSAGELOG_FILE here, because that can be 0, NULL, or
    // nullptr in release builds.
    QString path = QString(__FILE__);
    path = path.left(path.lastIndexOf('/') + 1);
    if (file.startsWith(path)) {
        file = file.mid(path.length());
    }

    // Time should be in UTC to save user privacy on log sharing
    QTime time = QDateTime::currentDateTime().toUTC().time();
    QString LogMsg =
        QString("[%1 UTC] %2:%3 : ").arg(time.toString("HH:mm:ss.zzz")).arg(file).arg(ctxt.line);
    switch (type) {
    case QtDebugMsg:
        LogMsg += "Debug";
        break;
    case QtWarningMsg:
        LogMsg += "Warning";
        break;
    case QtCriticalMsg:
        LogMsg += "Critical";
        break;
    case QtFatalMsg:
        LogMsg += "Fatal";
        break;
    default:
        break;
    }

    LogMsg += ": " + msg + "\n";
    QByteArray LogMsgBytes = LogMsg.toUtf8();
    fwrite(LogMsgBytes.constData(), 1, LogMsgBytes.size(), stderr);

#ifdef LOG_TO_FILE
    FILE* logFilePtr = logFileFile.load(); // atomically load the file pointer
    if (!logFilePtr) {
        logBufferMutex->lock();
        if (logBuffer)
            logBuffer->append(LogMsgBytes);

        logBufferMutex->unlock();
    } else {
        logBufferMutex->lock();
        if (logBuffer) {
            // empty logBuffer to file
            foreach (QByteArray msg, *logBuffer)
                fwrite(msg.constData(), 1, msg.size(), logFilePtr);

            delete logBuffer; // no longer needed
            logBuffer = nullptr;
        }
        logBufferMutex->unlock();

        fwrite(LogMsgBytes.constData(), 1, LogMsgBytes.size(), logFilePtr);
        fflush(logFilePtr);
    }
#endif
}

int main(int argc, char* argv[])
{

#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    qInstallMessageHandler(logMessageHandler);

    QApplication a(argc, argv);
    a.setApplicationName("qTox");
    a.setOrganizationName("Tox");
    a.setApplicationVersion("\nGit commit: " + QString(GIT_VERSION));

    // Install Unicode 6.1 supporting font
    // Keep this as close to the beginning of `main()` as possible, otherwise
    // on systems that have poor support for Unicode qTox will look bad.
    if (QFontDatabase::addApplicationFont("://font/DejaVuSans.ttf") == -1) {
        qWarning() << "Couldn't load font";
    }


#if defined(Q_OS_OSX)
    // TODO: Add setting to enable this feature.
    // osx::moveToAppFolder();
    osx::migrateProfiles();
#endif

    qsrand(time(0));
    Settings::getInstance();
    Translator::translate();

    // Process arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("qTox, version: " + QString(GIT_VERSION) + "\nBuilt: "
                                     + __TIME__ + " " + __DATE__);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("uri", QObject::tr("Tox URI to parse"));
    parser.addOption(
        QCommandLineOption("p", QObject::tr("Starts new instance and loads specified profile."),
                           QObject::tr("profile")));
    parser.process(a);

    IPC& ipc = IPC::getInstance();

    if (sodium_init() < 0) // For the auto-updater
    {
        qCritical() << "Can't init libsodium";
        return EXIT_FAILURE;
    }

#ifdef LOG_TO_FILE
    QString logFileDir = Settings::getInstance().getAppCacheDirPath();
    QDir(logFileDir).mkpath(".");

    QString logfile = logFileDir + "qtox.log";
    FILE* mainLogFilePtr = fopen(logfile.toLocal8Bit().constData(), "a");

    // Trim log file if over 1MB
    if (QFileInfo(logfile).size() > 1000000) {
        qDebug() << "Log file over 1MB, rotating...";

        // close old logfile (need for windows)
        if (mainLogFilePtr)
            fclose(mainLogFilePtr);

        QDir dir(logFileDir);

        // Check if log.1 already exists, and if so, delete it
        if (dir.remove(logFileDir + "qtox.log.1"))
            qDebug() << "Removed old log successfully";
        else
            qWarning() << "Unable to remove old log file";

        if (!dir.rename(logFileDir + "qtox.log", logFileDir + "qtox.log.1"))
            qCritical() << "Unable to move logs";

        // open a new logfile
        mainLogFilePtr = fopen(logfile.toLocal8Bit().constData(), "a");
    }

    if (!mainLogFilePtr)
        qCritical() << "Couldn't open logfile" << logfile;

    logFileFile.store(mainLogFilePtr); // atomically set the logFile
#endif

    // Windows platform plugins DLL hell fix
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());
    a.addLibraryPath("platforms");

    qDebug() << "built on: " << __TIME__ << __DATE__ << "(" << TIMESTAMP << ")";
    qDebug() << "commit: " << GIT_VERSION << "\n";


// Check whether we have an update waiting to be installed
#if AUTOUPDATE_ENABLED
    if (AutoUpdater::isLocalUpdateReady())
        AutoUpdater::installLocalUpdate(); ///< NORETURN
#endif


    QString profileName;
    bool autoLogin = Settings::getInstance().getAutoLogin();
    // Inter-process communication
    ipc.registerEventHandler("uri", &toxURIEventHandler);
    ipc.registerEventHandler("save", &toxSaveEventHandler);
    ipc.registerEventHandler("activate", &toxActivateEventHandler);

    uint32_t ipcDest = 0;
    QString eventType, firstParam;
    if (parser.isSet("p")) {
        profileName = parser.value("p");
        if (!Profile::exists(profileName)) {
            qCritical() << "-p profile" << profileName + ".tox"
                        << "doesn't exist";
            return EXIT_FAILURE;
        }
        ipcDest = Settings::makeProfileId(profileName);
        autoLogin = true;
    } else {
        profileName = Settings::getInstance().getCurrentProfile();
    }

    if (parser.positionalArguments().size() == 0) {
        eventType = "activate";
    } else {
        firstParam = parser.positionalArguments()[0];
        // Tox URIs. If there's already another qTox instance running, we ask it to handle the URI
        // and we exit
        // Otherwise we start a new qTox instance and process it ourselves
        if (firstParam.startsWith("tox:")) {
            eventType = "uri";
        } else if (firstParam.endsWith(".tox")) {
            eventType = "save";
        } else {
            qCritical() << "Invalid argument";
            return EXIT_FAILURE;
        }
    }

    if (!ipc.isCurrentOwner()) {
        time_t event = ipc.postEvent(eventType, firstParam.toUtf8(), ipcDest);
        // If someone else processed it, we're done here, no need to actually start qTox
        if (ipc.waitUntilAccepted(event, 2)) {
            qDebug() << "Event" << eventType << "was handled by other client.";
            return EXIT_SUCCESS;
        }
    }

    // Autologin
    if (autoLogin) {
        if (Profile::exists(profileName)) {
            if (!Profile::isEncrypted(profileName)) {
                Profile* profile = Profile::loadProfile(profileName);
                if (profile)
                    Nexus::getInstance().setProfile(profile);
            }
            Settings::getInstance().setCurrentProfile(profileName);
        }
    }

    Nexus& nexus = Nexus::getInstance();
    nexus.start();

    // Event was not handled by already running instance therefore we handle it ourselves
    if (eventType == "uri")
        handleToxURI(firstParam.toUtf8());
    else if (eventType == "save")
        handleToxSave(firstParam.toUtf8());

    // Run (unless we already quit before starting!)
    int errorcode = 0;
    if (nexus.isRunning())
        errorcode = a.exec();

    Nexus::destroyInstance();
    CameraSource::destroyInstance();
    Settings::destroyInstance();
    qDebug() << "Clean exit with status" << errorcode;

#ifdef LOG_TO_FILE
    logFileFile.store(nullptr); // atomically disable logging to file
    fclose(mainLogFilePtr);
#endif
    return errorcode;
}
