/*
    Copyright © 2013 by Maxim Biro <nurupo.contributions@gmail.com>
    Copyright © 2014-2019 by The qTox Project Contributors

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

#include "settings.h"
#include "src/core/core.h"
#include "src/core/corefile.h"
#include "src/nexus.h"
#include "src/persistence/profile.h"
#include "src/persistence/profilelocker.h"
#include "src/persistence/settingsserializer.h"
#include "src/persistence/smileypack.h"
#include "src/widget/gui.h"
#include "src/widget/style.h"
#ifdef QTOX_PLATFORM_EXT
#include "src/platform/autorun.h"
#endif
#include "src/ipc.h"

#include "util/compatiblerecursivemutex.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QList>
#include <QMutexLocker>
#include <QNetworkProxy>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QThread>
#include <QtCore/QCommandLineParser>

/**
 * @var QHash<QString, QByteArray> Settings::widgetSettings
 * @brief Assume all widgets have unique names
 * @warning Don't use it to save every single thing you want to save, use it
 * for some general purpose widgets, such as MainWindows or Splitters,
 * which have widget->saveX() and widget->loadX() methods.
 */

const QString Settings::globalSettingsFile = "qtox.ini";
Settings* Settings::settings{nullptr};
CompatibleRecursiveMutex Settings::bigLock;
QThread* Settings::settingsThread{nullptr};

Settings::Settings()
    : loaded(false)
    , useCustomDhtList{false}
    , makeToxPortable{false}
    , currentProfileId(0)
    , paths(*Paths::makePaths(Paths::Portable::NonPortable))
{
    settingsThread = new QThread();
    settingsThread->setObjectName("qTox Settings");
    settingsThread->start(QThread::LowPriority);
    moveToThread(settingsThread);
    loadGlobal();
}

Settings::~Settings()
{
    sync();
    settingsThread->exit(0);
    settingsThread->wait();
    delete settingsThread;
}

/**
 * @brief Returns the singleton instance.
 */
Settings& Settings::getInstance()
{
    if (!settings)
        settings = new Settings();

    return *settings;
}

void Settings::destroyInstance()
{
    delete settings;
    settings = nullptr;
}

void Settings::loadGlobal()
{
    QMutexLocker locker{&bigLock};

    if (loaded)
        return;

    createSettingsDir();

    makeToxPortable = Settings::isToxPortable();

    QDir dir(paths.getSettingsDirPath());
    QString filePath = dir.filePath(globalSettingsFile);

    // If no settings file exist -- use the default one
    if (!QFile(filePath).exists()) {
        qDebug() << "No settings file found, using defaults";
        filePath = ":/conf/" + globalSettingsFile;
    }

    qDebug() << "Loading settings from " + filePath;

    QSettings s(filePath, QSettings::IniFormat);
    s.setIniCodec("UTF-8");

    s.beginGroup("Login");
    {
        autoLogin = s.value("autoLogin", false).toBool();
    }
    s.endGroup();

    s.beginGroup("General");
    {
        translation = s.value("translation", "en").toString();
        showSystemTray = s.value("showSystemTray", true).toBool();
        autostartInTray = s.value("autostartInTray", false).toBool();
        closeToTray = s.value("closeToTray", false).toBool();
        if (currentProfile.isEmpty()) {
            currentProfile = s.value("currentProfile", "").toString();
            currentProfileId = makeProfileId(currentProfile);
        }
        autoAwayTime = s.value("autoAwayTime", 10).toInt();
        checkUpdates = s.value("checkUpdates", true).toBool();
        // note: notifySound and busySound UI elements are now under UI settings
        // page, but kept under General in settings file to be backwards compatible
        notifySound = s.value("notifySound", true).toBool();
        notifyHide = s.value("notifyHide", false).toBool();
        busySound = s.value("busySound", false).toBool();
        autoSaveEnabled = s.value("autoSaveEnabled", false).toBool();
        globalAutoAcceptDir = s.value("globalAutoAcceptDir",
                                      QStandardPaths::locate(QStandardPaths::HomeLocation, QString(),
                                                             QStandardPaths::LocateDirectory))
                                  .toString();
        autoAcceptMaxSize =
            static_cast<size_t>(s.value("autoAcceptMaxSize", 20 << 20 /*20 MB*/).toLongLong());
        stylePreference = static_cast<StyleType>(s.value("stylePreference", 1).toInt());
    }
    s.endGroup();

    s.beginGroup("Advanced");
    {
        makeToxPortable = s.value("makeToxPortable", false).toBool();
        enableIPv6 = s.value("enableIPv6", true).toBool();
        forceTCP = s.value("forceTCP", false).toBool();
        enableLanDiscovery = s.value("enableLanDiscovery", true).toBool();
    }
    s.endGroup();

    s.beginGroup("Widgets");
    {
        QList<QString> objectNames = s.childKeys();
        for (const QString& name : objectNames)
            widgetSettings[name] = s.value(name).toByteArray();
    }
    s.endGroup();

    s.beginGroup("GUI");
    {
        showWindow = s.value("showWindow", true).toBool();
        notify = s.value("notify", true).toBool();
        desktopNotify = s.value("desktopNotify", true).toBool();
        groupAlwaysNotify = s.value("groupAlwaysNotify", true).toBool();
        groupchatPosition = s.value("groupchatPosition", true).toBool();
        separateWindow = s.value("separateWindow", false).toBool();
        dontGroupWindows = s.value("dontGroupWindows", false).toBool();
        showIdenticons = s.value("showIdenticons", true).toBool();

        const QString DEFAULT_SMILEYS = ":/smileys/emojione/emoticons.xml";
        smileyPack = s.value("smileyPack", DEFAULT_SMILEYS).toString();
        if (!QFile::exists(smileyPack)) {
            smileyPack = DEFAULT_SMILEYS;
        }

        emojiFontPointSize = s.value("emojiFontPointSize", 24).toInt();
        firstColumnHandlePos = s.value("firstColumnHandlePos", 50).toInt();
        secondColumnHandlePosFromRight = s.value("secondColumnHandlePosFromRight", 50).toInt();
        timestampFormat = s.value("timestampFormat", "hh:mm:ss").toString();
        dateFormat = s.value("dateFormat", "yyyy-MM-dd").toString();
        minimizeOnClose = s.value("minimizeOnClose", false).toBool();
        minimizeToTray = s.value("minimizeToTray", false).toBool();
        lightTrayIcon = s.value("lightTrayIcon", false).toBool();
        useEmoticons = s.value("useEmoticons", true).toBool();
        statusChangeNotificationEnabled = s.value("statusChangeNotificationEnabled", false).toBool();
        spellCheckingEnabled = s.value("spellCheckingEnabled", true).toBool();
        themeColor = s.value("themeColor", 0).toInt();
        style = s.value("style", "").toString();
        if (style == "") // Default to Fusion if available, otherwise no style
        {
            if (QStyleFactory::keys().contains("Fusion"))
                style = "Fusion";
            else
                style = "None";
        }
        nameColors = s.value("nameColors", false).toBool();
        extraColor = s.value("extraColor", "#484848").toString();
        stateExtraColors = s.value("stateExtraColors", 0).toInt();
    }
    s.endGroup();

    s.beginGroup("Chat");
    {
        chatMessageFont = s.value("chatMessageFont", Style::getFont(Style::Big)).value<QFont>();
    }
    s.endGroup();

    s.beginGroup("State");
    {
        windowGeometry = s.value("windowGeometry", QByteArray()).toByteArray();
        windowState = s.value("windowState", QByteArray()).toByteArray();
        splitterState = s.value("splitterState", QByteArray()).toByteArray();
        dialogGeometry = s.value("dialogGeometry", QByteArray()).toByteArray();
        dialogSplitterState = s.value("dialogSplitterState", QByteArray()).toByteArray();
        dialogSettingsGeometry = s.value("dialogSettingsGeometry", QByteArray()).toByteArray();
    }
    s.endGroup();

    s.beginGroup("Audio");
    {
        inDev = s.value("inDev", "").toString();
        audioInDevEnabled = s.value("audioInDevEnabled", true).toBool();
        outDev = s.value("outDev", "").toString();
        audioOutDevEnabled = s.value("audioOutDevEnabled", true).toBool();
        audioInGainDecibel = s.value("inGain", 0).toReal();
        audioThreshold = s.value("audioThreshold", 0).toReal();
        outVolume = s.value("outVolume", 100).toInt();
        enableTestSound = s.value("enableTestSound", true).toBool();
        audioBitrate = s.value("audioBitrate", 64).toInt();
    }
    s.endGroup();

    s.beginGroup("Video");
    {
        videoDev = s.value("videoDev", "").toString();
        camVideoRes = s.value("camVideoRes", QRect()).toRect();
        screenRegion = s.value("screenRegion", QRect()).toRect();
        screenGrabbed = s.value("screenGrabbed", false).toBool();
        camVideoFPS = static_cast<quint16>(s.value("camVideoFPS", 0).toUInt());
    }
    s.endGroup();

    loaded = true;
}

bool Settings::isToxPortable()
{
    QString localSettingsPath = qApp->applicationDirPath() + QDir::separator() + globalSettingsFile;
    if (!QFile(localSettingsPath).exists()) {
        return false;
    }
    QSettings ps(localSettingsPath, QSettings::IniFormat);
    ps.setIniCodec("UTF-8");
    ps.beginGroup("Advanced");
    bool result = ps.value("makeToxPortable", false).toBool();
    ps.endGroup();
    return result;
}

void Settings::updateProfileData(Profile* profile, const QCommandLineParser* parser)
{
    QMutexLocker locker{&bigLock};

    if (profile == nullptr) {
        qWarning() << QString("Could not load new settings (profile change to nullptr)");
        return;
    }
    setCurrentProfile(profile->getName());
    saveGlobal();
    loadPersonal(profile->getName(), profile->getPasskey());
    if (parser) {
        applyCommandLineOptions(*parser);
    }
}

/**
 * Verifies that commandline proxy settings are at least reasonable. Does not verify provided IP
 * or hostname addresses are valid. Code duplication with Settings::applyCommandLineOptions, which
 * also verifies arguments, should be removed in a future refactor.
 * @param parser QCommandLineParser instance
 */
bool Settings::verifyProxySettings(const QCommandLineParser& parser)
{
    QString IPv6SettingString = parser.value("I").toLower();
    QString LANSettingString = parser.value("L").toLower();
    QString UDPSettingString = parser.value("U").toLower();
    QString proxySettingString = parser.value("proxy").toLower();
    QStringList proxySettingStrings = proxySettingString.split(":");

    const QString SOCKS5 = QStringLiteral("socks5");
    const QString HTTP = QStringLiteral("http");
    const QString NONE = QStringLiteral("none");
    const QString ON = QStringLiteral("on");
    const QString OFF = QStringLiteral("off");

    // Check for incompatible settings
    bool activeProxyType = false;

    if (parser.isSet("P")) {
        activeProxyType = proxySettingStrings[0] == SOCKS5 || proxySettingStrings[0] == HTTP;
    }

    if (parser.isSet("I")) {
        if (!(IPv6SettingString == ON || IPv6SettingString == OFF)) {
            qCritical() << "Unable to parse IPv6 setting.";
            return false;
        }
    }

    if (parser.isSet("U")) {
        if (!(UDPSettingString == ON || UDPSettingString == OFF)) {
            qCritical() << "Unable to parse UDP setting.";
            return false;
        }
    }

    if (parser.isSet("L")) {
        if (!(LANSettingString == ON || LANSettingString == OFF)) {
            qCritical() << "Unable to parse LAN setting.";
            return false;
        }
    }
    if (activeProxyType && UDPSettingString == ON) {
        qCritical() << "Cannot set UDP on with proxy.";
        return false;
    }

    if (activeProxyType && LANSettingString == ON) {
        qCritical() << "Cannot use LAN discovery with proxy.";
        return false;
    }

    if (LANSettingString == ON && UDPSettingString == OFF) {
        qCritical() << "Incompatible UDP/LAN settings.";
        return false;
    }

    if (parser.isSet("P")) {
        if (proxySettingStrings[0] == NONE) {
            // slightly lazy check here, accepting 'NONE[:.*]' is fine since no other
            // arguments will be investigated when proxy settings are applied.
            return true;
        }
        // Since the first argument isn't 'none', verify format of remaining arguments
        if (proxySettingStrings.size() != 3) {
            qCritical() << "Invalid number of proxy arguments.";
            return false;
        }

        if (!(proxySettingStrings[0] == SOCKS5 || proxySettingStrings[0] == HTTP)) {
            qCritical() << "Unable to parse proxy type.";
            return false;
        }

        // TODO(Kriby): Sanity check IPv4/IPv6 addresses/hostnames?

        int portNumber = proxySettingStrings[2].toInt();
        if (!(portNumber > 0 && portNumber < 65536)) {
            qCritical() << "Invalid port number range.";
        }
    }
    return true;
}

/**
 * Applies command line options on top of loaded settings. Fails without changes if attempting to
 * apply contradicting settings.
 * @param parser QCommandLineParser instance
 * @return Success indicator (success = true)
 */
bool Settings::applyCommandLineOptions(const QCommandLineParser& parser)
{
    if (!verifyProxySettings(parser)) {
        return false;
    }

    QString IPv6Setting = parser.value("I").toUpper();
    QString LANSetting = parser.value("L").toUpper();
    QString UDPSetting = parser.value("U").toUpper();
    QString proxySettingString = parser.value("proxy").toUpper();
    QStringList proxySettings = proxySettingString.split(":");

    const QString SOCKS5 = QStringLiteral("SOCKS5");
    const QString HTTP = QStringLiteral("HTTP");
    const QString NONE = QStringLiteral("NONE");
    const QString ON = QStringLiteral("ON");
    const QString OFF = QStringLiteral("OFF");


    if (parser.isSet("I")) {
        enableIPv6 = IPv6Setting == ON;
        qDebug() << QString("Setting IPv6 %1.").arg(IPv6Setting);
    }

    if (parser.isSet("P")) {
        qDebug() << QString("Setting proxy type to %1.").arg(proxySettings[0]);

        quint16 portNumber = 0;
        QString address = "";

        if (proxySettings[0] == NONE) {
            proxyType = ICoreSettings::ProxyType::ptNone;
        } else {
            if (proxySettings[0] == SOCKS5) {
                proxyType = ICoreSettings::ProxyType::ptSOCKS5;
            } else if (proxySettings[0] == HTTP) {
                proxyType = ICoreSettings::ProxyType::ptHTTP;
            } else {
                qCritical() << "Failed to set valid proxy type";
                assert(false); // verifyProxySettings should've made this impossible
            }

            forceTCP = true;
            enableLanDiscovery = false;

            address = proxySettings[1];
            portNumber = static_cast<quint16>(proxySettings[2].toInt());
        }


        proxyAddr = address;
        qDebug() << QString("Setting proxy address to %1.").arg(address);
        proxyPort = portNumber;
        qDebug() << QString("Setting port number to %1.").arg(portNumber);
    }

    if (parser.isSet("U")) {
        bool shouldForceTCP = UDPSetting == OFF;
        if (!shouldForceTCP && proxyType != ICoreSettings::ProxyType::ptNone) {
            qDebug() << "Cannot use UDP with proxy; disable proxy explicitly with '-P none'.";
        } else {
            forceTCP = shouldForceTCP;
            qDebug() << QString("Setting UDP %1.").arg(UDPSetting);
        }

        // LANSetting == ON is caught by verifyProxySettings, the OFF check removes needless debug
        if (shouldForceTCP && !(LANSetting == OFF) && enableLanDiscovery) {
            qDebug() << "Cannot perform LAN discovery without UDP; disabling LAN discovery.";
            enableLanDiscovery = false;
        }
    }

    if (parser.isSet("L")) {
        bool shouldEnableLAN = LANSetting == ON;

        if (shouldEnableLAN && proxyType != ICoreSettings::ProxyType::ptNone) {
            qDebug()
                << "Cannot use LAN discovery with proxy; disable proxy explicitly with '-P none'.";
        } else if (shouldEnableLAN && forceTCP) {
            qDebug() << "Cannot use LAN discovery without UDP; enable UDP explicitly with '-U on'.";
        } else {
            enableLanDiscovery = shouldEnableLAN;
            qDebug() << QString("Setting LAN Discovery %1.").arg(LANSetting);
        }
    }
    return true;
}

void Settings::loadPersonal(QString profileName, const ToxEncrypt* passKey)
{
    QMutexLocker locker{&bigLock};

    QDir dir(paths.getSettingsDirPath());
    QString filePath = dir.filePath(globalSettingsFile);

    // load from a profile specific friend data list if possible
    QString tmp = dir.filePath(profileName + ".ini");
    if (QFile(tmp).exists()) // otherwise, filePath remains the global file
        filePath = tmp;

    qDebug() << "Loading personal settings from" << filePath;

    SettingsSerializer ps(filePath, passKey);
    ps.load();
    friendLst.clear();

    ps.beginGroup("Privacy");
    {
        typingNotification = ps.value("typingNotification", true).toBool();
        enableLogging = ps.value("enableLogging", true).toBool();
        blackList = ps.value("blackList").toString().split('\n');
    }
    ps.endGroup();

    ps.beginGroup("Friends");
    {
        int size = ps.beginReadArray("Friend");
        friendLst.reserve(size);
        for (int i = 0; i < size; i++) {
            ps.setArrayIndex(i);
            friendProp fp{ps.value("addr").toString()};
            fp.alias = ps.value("alias").toString();
            fp.note = ps.value("note").toString();
            fp.autoAcceptDir = ps.value("autoAcceptDir").toString();

            if (fp.autoAcceptDir == "")
                fp.autoAcceptDir = ps.value("autoAccept").toString();

            fp.autoAcceptCall =
                Settings::AutoAcceptCallFlags(QFlag(ps.value("autoAcceptCall", 0).toInt()));
            fp.autoGroupInvite = ps.value("autoGroupInvite").toBool();
            fp.circleID = ps.value("circle", -1).toInt();

            if (getEnableLogging())
                fp.activity = ps.value("activity", QDateTime()).toDateTime();
            friendLst.insert(ToxId(fp.addr).getPublicKey().getByteArray(), fp);
        }
        ps.endArray();
    }
    ps.endGroup();

    ps.beginGroup("Requests");
    {
        int size = ps.beginReadArray("Request");
        friendRequests.clear();
        friendRequests.reserve(size);
        for (int i = 0; i < size; i++) {
            ps.setArrayIndex(i);
            Request request;
            request.address = ps.value("addr").toString();
            request.message = ps.value("message").toString();
            request.read = ps.value("read").toBool();
            friendRequests.push_back(request);
        }
        ps.endArray();
    }
    ps.endGroup();

    ps.beginGroup("GUI");
    {
        compactLayout = ps.value("compactLayout", true).toBool();
        sortingMode = static_cast<FriendListSortingMode>(
            ps.value("friendSortingMethod", static_cast<int>(FriendListSortingMode::Name)).toInt());
    }
    ps.endGroup();

    ps.beginGroup("Proxy");
    {
        proxyType = static_cast<ProxyType>(ps.value("proxyType", 0 /* ProxyType::None */).toInt());
        proxyType = fixInvalidProxyType(proxyType);
        proxyAddr = ps.value("proxyAddr", proxyAddr).toString();
        proxyPort = static_cast<quint16>(ps.value("proxyPort", proxyPort).toUInt());
    }
    ps.endGroup();

    ps.beginGroup("Circles");
    {
        int size = ps.beginReadArray("Circle");
        circleLst.clear();
        circleLst.reserve(size);
        for (int i = 0; i < size; i++) {
            ps.setArrayIndex(i);
            circleProp cp;
            cp.name = ps.value("name").toString();
            cp.expanded = ps.value("expanded", true).toBool();
            circleLst.push_back(cp);
        }
        ps.endArray();
    }
    ps.endGroup();
}

void Settings::resetToDefault()
{
    // To stop saving
    loaded = false;

    // Remove file with profile settings
    QDir dir(paths.getSettingsDirPath());
    Profile* profile = Nexus::getProfile();
    QString localPath = dir.filePath(profile->getName() + ".ini");
    QFile local(localPath);
    if (local.exists())
        local.remove();
}

/**
 * @brief Asynchronous, saves the global settings.
 */
void Settings::saveGlobal()
{
    if (QThread::currentThread() != settingsThread)
        return (void)QMetaObject::invokeMethod(&getInstance(), "saveGlobal");

    QMutexLocker locker{&bigLock};
    if (!loaded)
        return;

    QString path = paths.getSettingsDirPath() + globalSettingsFile;
    qDebug() << "Saving global settings at " + path;

    QSettings s(path, QSettings::IniFormat);
    s.setIniCodec("UTF-8");

    s.clear();

    s.beginGroup("Login");
    {
        s.setValue("autoLogin", autoLogin);
    }
    s.endGroup();

    s.beginGroup("General");
    {
        s.setValue("translation", translation);
        s.setValue("showSystemTray", showSystemTray);
        s.setValue("autostartInTray", autostartInTray);
        s.setValue("closeToTray", closeToTray);
        s.setValue("currentProfile", currentProfile);
        s.setValue("autoAwayTime", autoAwayTime);
        s.setValue("checkUpdates", checkUpdates);
        s.setValue("notifySound", notifySound);
        s.setValue("notifyHide", notifyHide);
        s.setValue("busySound", busySound);
        s.setValue("autoSaveEnabled", autoSaveEnabled);
        s.setValue("autoAcceptMaxSize", static_cast<qlonglong>(autoAcceptMaxSize));
        s.setValue("globalAutoAcceptDir", globalAutoAcceptDir);
        s.setValue("stylePreference", static_cast<int>(stylePreference));
    }
    s.endGroup();

    s.beginGroup("Advanced");
    {
        s.setValue("makeToxPortable", makeToxPortable);
        s.setValue("enableIPv6", enableIPv6);
        s.setValue("forceTCP", forceTCP);
        s.setValue("enableLanDiscovery", enableLanDiscovery);
        s.setValue("dbSyncType", static_cast<int>(dbSyncType));
    }
    s.endGroup();

    s.beginGroup("Widgets");
    {
        const QList<QString> widgetNames = widgetSettings.keys();
        for (const QString& name : widgetNames)
            s.setValue(name, widgetSettings.value(name));
    }
    s.endGroup();

    s.beginGroup("GUI");
    {
        s.setValue("showWindow", showWindow);
        s.setValue("notify", notify);
        s.setValue("desktopNotify", desktopNotify);
        s.setValue("groupAlwaysNotify", groupAlwaysNotify);
        s.setValue("separateWindow", separateWindow);
        s.setValue("dontGroupWindows", dontGroupWindows);
        s.setValue("groupchatPosition", groupchatPosition);
        s.setValue("showIdenticons", showIdenticons);

        s.setValue("smileyPack", smileyPack);
        s.setValue("emojiFontPointSize", emojiFontPointSize);
        s.setValue("firstColumnHandlePos", firstColumnHandlePos);
        s.setValue("secondColumnHandlePosFromRight", secondColumnHandlePosFromRight);
        s.setValue("timestampFormat", timestampFormat);
        s.setValue("dateFormat", dateFormat);
        s.setValue("minimizeOnClose", minimizeOnClose);
        s.setValue("minimizeToTray", minimizeToTray);
        s.setValue("lightTrayIcon", lightTrayIcon);
        s.setValue("useEmoticons", useEmoticons);
        s.setValue("themeColor", themeColor);
        s.setValue("style", style);
        s.setValue("nameColors", nameColors);
        s.setValue("extraColor", extraColor);
        s.setValue("stateExtraColors", stateExtraColors);
        s.setValue("statusChangeNotificationEnabled", statusChangeNotificationEnabled);
        s.setValue("spellCheckingEnabled", spellCheckingEnabled);
    }
    s.endGroup();

    s.beginGroup("Chat");
    {
        s.setValue("chatMessageFont", chatMessageFont);
    }
    s.endGroup();

    s.beginGroup("State");
    {
        s.setValue("windowGeometry", windowGeometry);
        s.setValue("windowState", windowState);
        s.setValue("splitterState", splitterState);
        s.setValue("dialogGeometry", dialogGeometry);
        s.setValue("dialogSplitterState", dialogSplitterState);
        s.setValue("dialogSettingsGeometry", dialogSettingsGeometry);
    }
    s.endGroup();

    s.beginGroup("Audio");
    {
        s.setValue("inDev", inDev);
        s.setValue("audioInDevEnabled", audioInDevEnabled);
        s.setValue("outDev", outDev);
        s.setValue("audioOutDevEnabled", audioOutDevEnabled);
        s.setValue("inGain", audioInGainDecibel);
        s.setValue("audioThreshold", audioThreshold);
        s.setValue("outVolume", outVolume);
        s.setValue("enableTestSound", enableTestSound);
        s.setValue("audioBitrate", audioBitrate);
    }
    s.endGroup();

    s.beginGroup("Video");
    {
        s.setValue("videoDev", videoDev);
        s.setValue("camVideoRes", camVideoRes);
        s.setValue("camVideoFPS", camVideoFPS);
        s.setValue("screenRegion", screenRegion);
        s.setValue("screenGrabbed", screenGrabbed);
    }
    s.endGroup();
}

/**
 * @brief Asynchronous, saves the current profile.
 */
void Settings::savePersonal()
{
    savePersonal(Nexus::getProfile());
}

/**
 * @brief Asynchronous, saves the profile.
 * @param profile Profile to save.
 */
void Settings::savePersonal(Profile* profile)
{
    if (!profile) {
        qDebug() << "Could not save personal settings because there is no active profile";
        return;
    }
    if (QThread::currentThread() != settingsThread)
        return (void)QMetaObject::invokeMethod(&getInstance(), "savePersonal",
                                               Q_ARG(Profile*, profile));
    savePersonal(profile->getName(), profile->getPasskey());
}

void Settings::savePersonal(QString profileName, const ToxEncrypt* passkey)
{
    QMutexLocker locker{&bigLock};
    if (!loaded)
        return;

    QString path = paths.getSettingsDirPath() + profileName + ".ini";

    qDebug() << "Saving personal settings at " << path;

    SettingsSerializer ps(path, passkey);
    ps.beginGroup("Friends");
    {
        ps.beginWriteArray("Friend", friendLst.size());
        int index = 0;
        for (auto& frnd : friendLst) {
            ps.setArrayIndex(index);
            ps.setValue("addr", frnd.addr);
            ps.setValue("alias", frnd.alias);
            ps.setValue("note", frnd.note);
            ps.setValue("autoAcceptDir", frnd.autoAcceptDir);
            ps.setValue("autoAcceptCall", static_cast<int>(frnd.autoAcceptCall));
            ps.setValue("autoGroupInvite", frnd.autoGroupInvite);
            ps.setValue("circle", frnd.circleID);

            if (getEnableLogging())
                ps.setValue("activity", frnd.activity);

            ++index;
        }
        ps.endArray();
    }
    ps.endGroup();

    ps.beginGroup("Requests");
    {
        ps.beginWriteArray("Request", friendRequests.size());
        int index = 0;
        for (auto& request : friendRequests) {
            ps.setArrayIndex(index);
            ps.setValue("addr", request.address);
            ps.setValue("message", request.message);
            ps.setValue("read", request.read);

            ++index;
        }
        ps.endArray();
    }
    ps.endGroup();

    ps.beginGroup("GUI");
    {
        ps.setValue("compactLayout", compactLayout);
        ps.setValue("friendSortingMethod", static_cast<int>(sortingMode));
    }
    ps.endGroup();

    ps.beginGroup("Proxy");
    {
        ps.setValue("proxyType", static_cast<int>(proxyType));
        ps.setValue("proxyAddr", proxyAddr);
        ps.setValue("proxyPort", proxyPort);
    }
    ps.endGroup();

    ps.beginGroup("Circles");
    {
        ps.beginWriteArray("Circle", circleLst.size());
        int index = 0;
        for (auto& circle : circleLst) {
            ps.setArrayIndex(index);
            ps.setValue("name", circle.name);
            ps.setValue("expanded", circle.expanded);
            ++index;
        }
        ps.endArray();
    }
    ps.endGroup();

    ps.beginGroup("Privacy");
    {
        ps.setValue("typingNotification", typingNotification);
        ps.setValue("enableLogging", enableLogging);
        ps.setValue("blackList", blackList.join('\n'));
    }
    ps.endGroup();
    ps.save();
}

uint32_t Settings::makeProfileId(const QString& profile)
{
    QByteArray data = QCryptographicHash::hash(profile.toUtf8(), QCryptographicHash::Md5);
    const uint32_t* dwords = reinterpret_cast<const uint32_t*>(data.constData());
    return dwords[0] ^ dwords[1] ^ dwords[2] ^ dwords[3];
}

Paths& Settings::getPaths()
{
    return paths;
}

bool Settings::getEnableTestSound() const
{
    QMutexLocker locker{&bigLock};
    return enableTestSound;
}

void Settings::setEnableTestSound(bool newValue)
{
    if (setVal(enableTestSound, newValue)) {
        emit enableTestSoundChanged(newValue);
    }
}

bool Settings::getEnableIPv6() const
{
    QMutexLocker locker{&bigLock};
    return enableIPv6;
}

void Settings::setEnableIPv6(bool enabled)
{
    if (setVal(enableIPv6, enabled)) {
        emit enableIPv6Changed(enabled);
    }
}

bool Settings::getMakeToxPortable() const
{
    QMutexLocker locker{&bigLock};
    return makeToxPortable;
}

void Settings::setMakeToxPortable(bool newValue)
{
    bool changed = false;
    {
        QMutexLocker locker{&bigLock};

        if (newValue != makeToxPortable) {
            QFile(paths.getSettingsDirPath() + globalSettingsFile).remove();
            makeToxPortable = newValue;
            saveGlobal();

            changed = true;
        }
    }
    if (changed) {
        emit makeToxPortableChanged(newValue);
    }
}

bool Settings::getAutorun() const
{
    QMutexLocker locker{&bigLock};

#ifdef QTOX_PLATFORM_EXT
    return Platform::getAutorun();
#else
    return false;
#endif
}

void Settings::setAutorun(bool newValue)
{
#ifdef QTOX_PLATFORM_EXT
    bool autorun = Platform::getAutorun();

    if (newValue != autorun) {
        Platform::setAutorun(newValue);
        emit autorunChanged(autorun);
    }
#else
    Q_UNUSED(newValue)
#endif
}

bool Settings::getAutostartInTray() const
{
    QMutexLocker locker{&bigLock};
    return autostartInTray;
}

QString Settings::getStyle() const
{
    QMutexLocker locker{&bigLock};
    return style;
}

void Settings::setStyle(const QString& newStyle)
{
    if (setVal(style, newStyle)) {
        emit styleChanged(style);
    }
}

bool Settings::getShowSystemTray() const
{
    QMutexLocker locker{&bigLock};
    return showSystemTray;
}

void Settings::setShowSystemTray(bool newValue)
{
    if (setVal(showSystemTray, newValue)) {
        emit showSystemTrayChanged(newValue);
    }
}

void Settings::setUseEmoticons(bool newValue)
{
    if (setVal(useEmoticons, newValue)) {
        emit useEmoticonsChanged(newValue);
    }
}

bool Settings::getUseEmoticons() const
{
    QMutexLocker locker{&bigLock};
    return useEmoticons;
}

void Settings::setAutoSaveEnabled(bool newValue)
{
    if (setVal(autoSaveEnabled, newValue)) {
        emit autoSaveEnabledChanged(newValue);
    }
}

bool Settings::getAutoSaveEnabled() const
{
    QMutexLocker locker{&bigLock};
    return autoSaveEnabled;
}

void Settings::setAutostartInTray(bool newValue)
{
    if (setVal(autostartInTray, newValue)) {
        emit autostartInTrayChanged(newValue);
    }
}

bool Settings::getCloseToTray() const
{
    QMutexLocker locker{&bigLock};
    return closeToTray;
}

void Settings::setCloseToTray(bool newValue)
{
    if (setVal(closeToTray, newValue)) {
        emit closeToTrayChanged(newValue);
    }
}

bool Settings::getMinimizeToTray() const
{
    QMutexLocker locker{&bigLock};
    return minimizeToTray;
}

void Settings::setMinimizeToTray(bool newValue)
{
    if (setVal(minimizeToTray, newValue)) {
        emit minimizeToTrayChanged(newValue);
    }
}

bool Settings::getLightTrayIcon() const
{
    QMutexLocker locker{&bigLock};
    return lightTrayIcon;
}

void Settings::setLightTrayIcon(bool newValue)
{
    if (setVal(lightTrayIcon, newValue)) {
        emit lightTrayIconChanged(newValue);
    }
}

bool Settings::getStatusChangeNotificationEnabled() const
{
    QMutexLocker locker{&bigLock};
    return statusChangeNotificationEnabled;
}

void Settings::setStatusChangeNotificationEnabled(bool newValue)
{
    if (setVal(statusChangeNotificationEnabled, newValue)) {
        emit statusChangeNotificationEnabledChanged(newValue);
    }
}

bool Settings::getSpellCheckingEnabled() const
{
    const QMutexLocker locker{&bigLock};
    return spellCheckingEnabled;
}

void Settings::setSpellCheckingEnabled(bool newValue)
{
    if (setVal(spellCheckingEnabled, newValue)) {
        emit spellCheckingEnabledChanged(newValue);
    }
}

bool Settings::getNotifySound() const
{
    QMutexLocker locker{&bigLock};
    return notifySound;
}

void Settings::setNotifySound(bool newValue)
{
    if (setVal(notifySound, newValue)) {
        emit notifySoundChanged(newValue);
    }
}

bool Settings::getNotifyHide() const
{
    QMutexLocker locker{&bigLock};
    return notifyHide;
}

void Settings::setNotifyHide(bool newValue)
{
    if (setVal(notifyHide, newValue)) {
        emit notifyHideChanged(newValue);
    }
}

bool Settings::getBusySound() const
{
    QMutexLocker locker{&bigLock};
    return busySound;
}

void Settings::setBusySound(bool newValue)
{
    if (setVal(busySound, newValue)) {
        emit busySoundChanged(newValue);
    }
}

bool Settings::getGroupAlwaysNotify() const
{
    QMutexLocker locker{&bigLock};
    return groupAlwaysNotify;
}

void Settings::setGroupAlwaysNotify(bool newValue)
{
    if (setVal(groupAlwaysNotify, newValue)) {
        emit groupAlwaysNotifyChanged(newValue);
    }
}

QString Settings::getTranslation() const
{
    QMutexLocker locker{&bigLock};
    return translation;
}

void Settings::setTranslation(const QString& newValue)
{
    if (setVal(translation, newValue)) {
        emit translationChanged(newValue);
    }
}

bool Settings::getForceTCP() const
{
    QMutexLocker locker{&bigLock};
    return forceTCP;
}

void Settings::setForceTCP(bool enabled)
{
    if (setVal(forceTCP, enabled)) {
        emit forceTCPChanged(enabled);
    }
}

bool Settings::getEnableLanDiscovery() const
{
    QMutexLocker locker{&bigLock};
    return enableLanDiscovery;
}

void Settings::setEnableLanDiscovery(bool enabled)
{
    if (setVal(enableLanDiscovery, enabled)) {
        emit enableLanDiscoveryChanged(enabled);
    }
}

QNetworkProxy Settings::getProxy() const
{
    QMutexLocker locker{&bigLock};

    QNetworkProxy proxy;
    switch (Settings::getProxyType()) {
    case ProxyType::ptNone:
        proxy.setType(QNetworkProxy::NoProxy);
        break;
    case ProxyType::ptSOCKS5:
        proxy.setType(QNetworkProxy::Socks5Proxy);
        break;
    case ProxyType::ptHTTP:
        proxy.setType(QNetworkProxy::HttpProxy);
        break;
    default:
        proxy.setType(QNetworkProxy::NoProxy);
        qWarning() << "Invalid proxy type, setting to NoProxy";
        break;
    }

    proxy.setHostName(Settings::getProxyAddr());
    proxy.setPort(Settings::getProxyPort());
    return proxy;
}

Settings::ProxyType Settings::getProxyType() const
{
    QMutexLocker locker{&bigLock};
    return proxyType;
}

void Settings::setProxyType(ProxyType newValue)
{
    if (setVal(proxyType, newValue)) {
        emit proxyTypeChanged(newValue);
    }
}

QString Settings::getProxyAddr() const
{
    QMutexLocker locker{&bigLock};
    return proxyAddr;
}

void Settings::setProxyAddr(const QString& address)
{
    if (setVal(proxyAddr, address)) {
        emit proxyAddressChanged(address);
    }
}

quint16 Settings::getProxyPort() const
{
    QMutexLocker locker{&bigLock};
    return proxyPort;
}

void Settings::setProxyPort(quint16 port)
{
    if (setVal(proxyPort, port)) {
        emit proxyPortChanged(port);
    }
}

QString Settings::getCurrentProfile() const
{
    QMutexLocker locker{&bigLock};
    return currentProfile;
}

uint32_t Settings::getCurrentProfileId() const
{
    QMutexLocker locker{&bigLock};
    return currentProfileId;
}

void Settings::setCurrentProfile(const QString& profile)
{
    bool updated = false;
    uint32_t newProfileId = 0;
    {
        QMutexLocker locker{&bigLock};

        if (profile != currentProfile) {
            currentProfile = profile;
            currentProfileId = makeProfileId(currentProfile);
            newProfileId = currentProfileId;
            updated = true;
        }
    }
    if (updated) {
        emit currentProfileIdChanged(newProfileId);
    }
}

bool Settings::getEnableLogging() const
{
    QMutexLocker locker{&bigLock};
    return enableLogging;
}

void Settings::setEnableLogging(bool newValue)
{
    if (setVal(enableLogging, newValue)) {
        emit enableLoggingChanged(newValue);
    }
}

int Settings::getAutoAwayTime() const
{
    QMutexLocker locker{&bigLock};
    return autoAwayTime;
}

/**
 * @brief Sets how long the user may stay idle, before online status is set to "away".
 * @param[in] newValue  the user idle duration in minutes
 * @note Values < 0 default to 10 minutes.
 */
void Settings::setAutoAwayTime(int newValue)
{
    if (newValue < 0) {
        newValue = 10;
    }

    if (setVal(autoAwayTime, newValue)) {
        emit autoAwayTimeChanged(newValue);
    }
}

QString Settings::getAutoAcceptDir(const ToxPk& id) const
{
    QMutexLocker locker{&bigLock};

    auto it = friendLst.find(id.getByteArray());
    if (it != friendLst.end())
        return it->autoAcceptDir;

    return QString();
}

void Settings::setAutoAcceptDir(const ToxPk& id, const QString& dir)
{
    bool updated = false;
    {
        QMutexLocker locker{&bigLock};

        auto& frnd = getOrInsertFriendPropRef(id);

        if (frnd.autoAcceptDir != dir) {
            frnd.autoAcceptDir = dir;
            updated = true;
        }
    }
    if (updated) {
        emit autoAcceptDirChanged(id, dir);
    }
}

Settings::AutoAcceptCallFlags Settings::getAutoAcceptCall(const ToxPk& id) const
{
    QMutexLocker locker{&bigLock};

    auto it = friendLst.find(id.getByteArray());
    if (it != friendLst.end())
        return it->autoAcceptCall;

    return Settings::AutoAcceptCallFlags();
}

void Settings::setAutoAcceptCall(const ToxPk& id, AutoAcceptCallFlags accept)
{
    bool updated = false;
    {
        QMutexLocker locker{&bigLock};

        auto& frnd = getOrInsertFriendPropRef(id);

        if (frnd.autoAcceptCall != accept) {
            frnd.autoAcceptCall = accept;
            updated = true;
        }
    }
    if (updated) {
        emit autoAcceptCallChanged(id, accept);
    }
}

bool Settings::getAutoGroupInvite(const ToxPk& id) const
{
    QMutexLocker locker{&bigLock};

    auto it = friendLst.find(id.getByteArray());
    if (it != friendLst.end()) {
        return it->autoGroupInvite;
    }

    return false;
}

void Settings::setAutoGroupInvite(const ToxPk& id, bool accept)
{
    bool updated = false;
    {
        QMutexLocker locker{&bigLock};

        auto& frnd = getOrInsertFriendPropRef(id);

        if (frnd.autoGroupInvite != accept) {
            frnd.autoGroupInvite = accept;
            updated = true;
        }
    }

    if (updated) {
        emit autoGroupInviteChanged(id, accept);
    }
}

QString Settings::getContactNote(const ToxPk& id) const
{
    QMutexLocker locker{&bigLock};

    auto it = friendLst.find(id.getByteArray());
    if (it != friendLst.end())
        return it->note;

    return QString();
}

void Settings::setContactNote(const ToxPk& id, const QString& note)
{
    bool updated = false;
    {
        QMutexLocker locker{&bigLock};

        auto& frnd = getOrInsertFriendPropRef(id);

        if (frnd.note != note) {
            frnd.note = note;
            updated = true;
        }
    }
    if (updated) {
        emit contactNoteChanged(id, note);
    }
}

QString Settings::getGlobalAutoAcceptDir() const
{
    QMutexLocker locker{&bigLock};
    return globalAutoAcceptDir;
}

void Settings::setGlobalAutoAcceptDir(const QString& newValue)
{
    if (setVal(globalAutoAcceptDir, newValue)) {
        emit globalAutoAcceptDirChanged(newValue);
    }
}

size_t Settings::getMaxAutoAcceptSize() const
{
    QMutexLocker locker{&bigLock};
    return autoAcceptMaxSize;
}

void Settings::setMaxAutoAcceptSize(size_t size)
{
    if (setVal(autoAcceptMaxSize, size)) {
        emit autoAcceptMaxSizeChanged(size);
    }
}

const QFont& Settings::getChatMessageFont() const
{
    QMutexLocker locker(&bigLock);
    return chatMessageFont;
}

void Settings::setChatMessageFont(const QFont& font)
{
    if (setVal(chatMessageFont, font)) {
        emit chatMessageFontChanged(font);
    }
}

void Settings::setWidgetData(const QString& uniqueName, const QByteArray& data)
{
    bool updated = false;
    {
        QMutexLocker locker{&bigLock};

        if (!widgetSettings.contains(uniqueName) || widgetSettings[uniqueName] != data) {
            widgetSettings[uniqueName] = data;
            updated = true;
        }
    }
    if (updated) {
        emit widgetDataChanged(uniqueName);
    }
}

QByteArray Settings::getWidgetData(const QString& uniqueName) const
{
    QMutexLocker locker{&bigLock};
    return widgetSettings.value(uniqueName);
}

QString Settings::getSmileyPack() const
{
    QMutexLocker locker{&bigLock};
    return smileyPack;
}

void Settings::setSmileyPack(const QString& value)
{
    if (setVal(smileyPack, value)) {
        emit smileyPackChanged(value);
    }
}

int Settings::getEmojiFontPointSize() const
{
    QMutexLocker locker{&bigLock};
    return emojiFontPointSize;
}

void Settings::setEmojiFontPointSize(int value)
{
    if (setVal(emojiFontPointSize, value)) {
        emit emojiFontPointSizeChanged(value);
    }
}

const QString& Settings::getTimestampFormat() const
{
    QMutexLocker locker{&bigLock};
    return timestampFormat;
}

void Settings::setTimestampFormat(const QString& format)
{
    if (setVal(timestampFormat, format)) {
        emit timestampFormatChanged(format);
    }
}

const QString& Settings::getDateFormat() const
{
    QMutexLocker locker{&bigLock};
    return dateFormat;
}

void Settings::setDateFormat(const QString& format)
{
    if (setVal(dateFormat, format)) {
        emit dateFormatChanged(format);
    }
}

Settings::StyleType Settings::getStylePreference() const
{
    QMutexLocker locker{&bigLock};
    return stylePreference;
}

void Settings::setStylePreference(StyleType newValue)
{
    if (setVal(stylePreference, newValue)) {
        emit stylePreferenceChanged(newValue);
    }
}

QByteArray Settings::getWindowGeometry() const
{
    QMutexLocker locker{&bigLock};
    return windowGeometry;
}

void Settings::setWindowGeometry(const QByteArray& value)
{
    if (setVal(windowGeometry, value)) {
        emit windowGeometryChanged(value);
    }
}

QByteArray Settings::getWindowState() const
{
    QMutexLocker locker{&bigLock};
    return windowState;
}

void Settings::setWindowState(const QByteArray& value)
{
    if (setVal(windowState, value)) {
        emit windowStateChanged(value);
    }
}

bool Settings::getCheckUpdates() const
{
    QMutexLocker locker{&bigLock};
    return checkUpdates;
}

void Settings::setCheckUpdates(bool newValue)
{
    if (setVal(checkUpdates, newValue)) {
        emit checkUpdatesChanged(newValue);
    }
}

bool Settings::getNotify() const
{
    QMutexLocker locker{&bigLock};
    return notify;
}

void Settings::setNotify(bool newValue)
{
    if (setVal(notify, newValue)) {
        emit notifyChanged(newValue);
    }
}

bool Settings::getShowWindow() const
{
    QMutexLocker locker{&bigLock};
    return showWindow;
}

void Settings::setShowWindow(bool newValue)
{
    if (setVal(showWindow, newValue)) {
        emit showWindowChanged(newValue);
    }
}

bool Settings::getDesktopNotify() const
{
    QMutexLocker locker{&bigLock};
    return desktopNotify;
}

void Settings::setDesktopNotify(bool enabled)
{
    if (setVal(desktopNotify, enabled)) {
        emit desktopNotifyChanged(enabled);
    }
}

QByteArray Settings::getSplitterState() const
{
    QMutexLocker locker{&bigLock};
    return splitterState;
}

void Settings::setSplitterState(const QByteArray& value)
{
    if (setVal(splitterState, value)) {
        emit splitterStateChanged(value);
    }
}

QByteArray Settings::getDialogGeometry() const
{
    QMutexLocker locker{&bigLock};
    return dialogGeometry;
}

void Settings::setDialogGeometry(const QByteArray& value)
{
    if (setVal(dialogGeometry, value)) {
        emit dialogGeometryChanged(value);
    }
}

QByteArray Settings::getDialogSplitterState() const
{
    QMutexLocker locker{&bigLock};
    return dialogSplitterState;
}

void Settings::setDialogSplitterState(const QByteArray& value)
{
    if (setVal(dialogSplitterState, value)) {
        emit dialogSplitterStateChanged(value);
    }
}

QByteArray Settings::getDialogSettingsGeometry() const
{
    QMutexLocker locker{&bigLock};
    return dialogSettingsGeometry;
}

void Settings::setDialogSettingsGeometry(const QByteArray& value)
{
    if (setVal(dialogSettingsGeometry, value)) {
        emit dialogSettingsGeometryChanged(value);
    }
}

bool Settings::getMinimizeOnClose() const
{
    QMutexLocker locker{&bigLock};
    return minimizeOnClose;
}

void Settings::setMinimizeOnClose(bool newValue)
{
    if (setVal(minimizeOnClose, newValue)) {
        emit minimizeOnCloseChanged(newValue);
    }
}

bool Settings::getTypingNotification() const
{
    QMutexLocker locker{&bigLock};
    return typingNotification;
}

void Settings::setTypingNotification(bool enabled)
{
    if (setVal(typingNotification, enabled)) {
        emit typingNotificationChanged(enabled);
    }
}

QStringList Settings::getBlackList() const
{
    QMutexLocker locker{&bigLock};
    return blackList;
}

void Settings::setBlackList(const QStringList& blist)
{
    if (setVal(blackList, blist)) {
        emit blackListChanged(blist);
    }
}

QString Settings::getInDev() const
{
    QMutexLocker locker{&bigLock};
    return inDev;
}

void Settings::setInDev(const QString& deviceSpecifier)
{
    if (setVal(inDev, deviceSpecifier)) {
        emit inDevChanged(deviceSpecifier);
    }
}

bool Settings::getAudioInDevEnabled() const
{
    QMutexLocker locker(&bigLock);
    return audioInDevEnabled;
}

void Settings::setAudioInDevEnabled(bool enabled)
{
    if (setVal(audioInDevEnabled, enabled)) {
        emit audioInDevEnabledChanged(enabled);
    }
}

qreal Settings::getAudioInGainDecibel() const
{
    QMutexLocker locker{&bigLock};
    return audioInGainDecibel;
}

void Settings::setAudioInGainDecibel(qreal dB)
{
    if (setVal(audioInGainDecibel, dB)) {
        emit audioInGainDecibelChanged(audioInGainDecibel);
    }
}

qreal Settings::getAudioThreshold() const
{
    QMutexLocker locker{&bigLock};
    return audioThreshold;
}

void Settings::setAudioThreshold(qreal percent)
{
    if (setVal(audioThreshold, percent)) {
        emit audioThresholdChanged(percent);
    }
}

QString Settings::getVideoDev() const
{
    QMutexLocker locker{&bigLock};
    return videoDev;
}

void Settings::setVideoDev(const QString& deviceSpecifier)
{
    if (setVal(videoDev, deviceSpecifier)) {
        emit videoDevChanged(deviceSpecifier);
    }
}

QString Settings::getOutDev() const
{
    QMutexLocker locker{&bigLock};
    return outDev;
}

void Settings::setOutDev(const QString& deviceSpecifier)
{
    if (setVal(outDev, deviceSpecifier)) {
        emit outDevChanged(deviceSpecifier);
    }
}

bool Settings::getAudioOutDevEnabled() const
{
    QMutexLocker locker(&bigLock);
    return audioOutDevEnabled;
}

void Settings::setAudioOutDevEnabled(bool enabled)
{
    if (setVal(audioOutDevEnabled, enabled)) {
        emit audioOutDevEnabledChanged(enabled);
    }
}

int Settings::getOutVolume() const
{
    QMutexLocker locker{&bigLock};
    return outVolume;
}

void Settings::setOutVolume(int volume)
{
    if (setVal(outVolume, volume)) {
        emit outVolumeChanged(volume);
    }
}

int Settings::getAudioBitrate() const
{
    const QMutexLocker locker{&bigLock};
    return audioBitrate;
}

void Settings::setAudioBitrate(int bitrate)
{
    if (setVal(audioBitrate, bitrate)) {
        emit audioBitrateChanged(bitrate);
    }
}

QRect Settings::getScreenRegion() const
{
    QMutexLocker locker(&bigLock);
    return screenRegion;
}

void Settings::setScreenRegion(const QRect& value)
{
    if (setVal(screenRegion, value)) {
        emit screenRegionChanged(value);
    }
}

bool Settings::getScreenGrabbed() const
{
    QMutexLocker locker(&bigLock);
    return screenGrabbed;
}

void Settings::setScreenGrabbed(bool value)
{
    if (setVal(screenGrabbed, value)) {
        emit screenGrabbedChanged(value);
    }
}

QRect Settings::getCamVideoRes() const
{
    QMutexLocker locker{&bigLock};
    return camVideoRes;
}

void Settings::setCamVideoRes(QRect newValue)
{
    if (setVal(camVideoRes, newValue)) {
        emit camVideoResChanged(newValue);
    }
}

float Settings::getCamVideoFPS() const
{
    QMutexLocker locker{&bigLock};
    return camVideoFPS;
}

void Settings::setCamVideoFPS(float newValue)
{
    if (setVal(camVideoFPS, newValue)) {
        emit camVideoFPSChanged(newValue);
    }
}

QString Settings::getFriendAddress(const QString& publicKey) const
{
    QMutexLocker locker{&bigLock};
    // TODO: using ToxId here is a hack
    QByteArray key = ToxId(publicKey).getPublicKey().getByteArray();
    auto it = friendLst.find(key);
    if (it != friendLst.end())
        return it->addr;

    return QString();
}

void Settings::updateFriendAddress(const QString& newAddr)
{
    QMutexLocker locker{&bigLock};
    // TODO: using ToxId here is a hack
    auto key = ToxId(newAddr).getPublicKey();
    auto& frnd = getOrInsertFriendPropRef(key);
    frnd.addr = newAddr;
}

QString Settings::getFriendAlias(const ToxPk& id) const
{
    QMutexLocker locker{&bigLock};
    auto it = friendLst.find(id.getByteArray());
    if (it != friendLst.end())
        return it->alias;

    return QString();
}

void Settings::setFriendAlias(const ToxPk& id, const QString& alias)
{
    QMutexLocker locker{&bigLock};
    auto& frnd = getOrInsertFriendPropRef(id);
    frnd.alias = alias;
}

int Settings::getFriendCircleID(const ToxPk& id) const
{
    QMutexLocker locker{&bigLock};
    auto it = friendLst.find(id.getByteArray());
    if (it != friendLst.end())
        return it->circleID;

    return -1;
}

void Settings::setFriendCircleID(const ToxPk& id, int circleID)
{
    QMutexLocker locker{&bigLock};
    auto& frnd = getOrInsertFriendPropRef(id);
    frnd.circleID = circleID;
}

QDateTime Settings::getFriendActivity(const ToxPk& id) const
{
    QMutexLocker locker{&bigLock};
    auto it = friendLst.find(id.getByteArray());
    if (it != friendLst.end())
        return it->activity;

    return QDateTime();
}

void Settings::setFriendActivity(const ToxPk& id, const QDateTime& activity)
{
    QMutexLocker locker{&bigLock};
    auto& frnd = getOrInsertFriendPropRef(id);
    frnd.activity = activity;
}

void Settings::saveFriendSettings(const ToxPk& id)
{
    Q_UNUSED(id)
    savePersonal();
}

void Settings::removeFriendSettings(const ToxPk& id)
{
    QMutexLocker locker{&bigLock};
    friendLst.remove(id.getByteArray());
}

bool Settings::getCompactLayout() const
{
    QMutexLocker locker{&bigLock};
    return compactLayout;
}

void Settings::setCompactLayout(bool value)
{
    if (setVal(compactLayout, value)) {
        emit compactLayoutChanged(value);
    }
}

Settings::FriendListSortingMode Settings::getFriendSortingMode() const
{
    QMutexLocker locker{&bigLock};
    return sortingMode;
}

void Settings::setFriendSortingMode(FriendListSortingMode mode)
{
    if (setVal(sortingMode, mode)) {
        emit sortingModeChanged(mode);
    }
}

bool Settings::getSeparateWindow() const
{
    QMutexLocker locker{&bigLock};
    return separateWindow;
}

void Settings::setSeparateWindow(bool value)
{
    if (setVal(separateWindow, value)) {
        emit separateWindowChanged(value);
    }
}

bool Settings::getDontGroupWindows() const
{
    QMutexLocker locker{&bigLock};
    return dontGroupWindows;
}

void Settings::setDontGroupWindows(bool value)
{
    if (setVal(dontGroupWindows, value)) {
        emit dontGroupWindowsChanged(value);
    }
}

bool Settings::getGroupchatPosition() const
{
    QMutexLocker locker{&bigLock};
    return groupchatPosition;
}

void Settings::setGroupchatPosition(bool value)
{
    if (setVal(groupchatPosition, value)) {
        emit groupchatPositionChanged(value);
    }
}

bool Settings::getShowIdenticons() const
{
    const QMutexLocker locker{&bigLock};
    return showIdenticons;
}

void Settings::setShowIdenticons(bool value)
{
    if (setVal(showIdenticons, value)) {
        emit showIdenticonsChanged(value);
    }
}

int Settings::getCircleCount() const
{
    QMutexLocker locker{&bigLock};
    return circleLst.size();
}

QString Settings::getCircleName(int id) const
{
    QMutexLocker locker{&bigLock};
    return circleLst[id].name;
}

void Settings::setCircleName(int id, const QString& name)
{
    QMutexLocker locker{&bigLock};
    circleLst[id].name = name;
    savePersonal();
}

int Settings::addCircle(const QString& name)
{
    QMutexLocker locker{&bigLock};

    circleProp cp;
    cp.expanded = false;

    if (name.isEmpty())
        cp.name = tr("Circle #%1").arg(circleLst.count() + 1);
    else
        cp.name = name;

    circleLst.append(cp);
    savePersonal();
    return circleLst.count() - 1;
}

bool Settings::getCircleExpanded(int id) const
{
    QMutexLocker locker{&bigLock};
    return circleLst[id].expanded;
}

void Settings::setCircleExpanded(int id, bool expanded)
{
    QMutexLocker locker{&bigLock};
    circleLst[id].expanded = expanded;
}

bool Settings::addFriendRequest(const QString& friendAddress, const QString& message)
{
    QMutexLocker locker{&bigLock};

    for (auto queued : friendRequests) {
        if (queued.address == friendAddress) {
            queued.message = message;
            queued.read = false;
            return false;
        }
    }

    Request request;
    request.address = friendAddress;
    request.message = message;
    request.read = false;

    friendRequests.push_back(request);
    return true;
}

unsigned int Settings::getUnreadFriendRequests() const
{
    QMutexLocker locker{&bigLock};
    unsigned int unreadFriendRequests = 0;
    for (auto request : friendRequests)
        if (!request.read)
            ++unreadFriendRequests;

    return unreadFriendRequests;
}

Settings::Request Settings::getFriendRequest(int index) const
{
    QMutexLocker locker{&bigLock};
    return friendRequests.at(index);
}

int Settings::getFriendRequestSize() const
{
    QMutexLocker locker{&bigLock};
    return friendRequests.size();
}

void Settings::clearUnreadFriendRequests()
{
    QMutexLocker locker{&bigLock};

    for (auto& request : friendRequests)
        request.read = true;
}

void Settings::removeFriendRequest(int index)
{
    QMutexLocker locker{&bigLock};
    friendRequests.removeAt(index);
}

void Settings::readFriendRequest(int index)
{
    QMutexLocker locker{&bigLock};
    friendRequests[index].read = true;
}

int Settings::removeCircle(int id)
{
    // Replace index with last one and remove last one instead.
    // This gives you contiguous ids all the time.
    circleLst[id] = circleLst.last();
    circleLst.pop_back();
    savePersonal();
    return circleLst.count();
}

int Settings::getThemeColor() const
{
    QMutexLocker locker{&bigLock};
    return themeColor;
}

void Settings::setThemeColor(int value)
{
    if (setVal(themeColor, value)) {
        emit themeColorChanged(value);
    }
}

bool Settings::getAutoLogin() const
{
    QMutexLocker locker{&bigLock};
    return autoLogin;
}

void Settings::setAutoLogin(bool state)
{
    if (setVal(autoLogin, state)) {
        emit autoLoginChanged(state);
    }
}

void Settings::setEnableGroupChatsColor(bool state)
{
    if (setVal(nameColors, state)) {
        emit nameColorsChanged(state);
    }
}

bool Settings::getEnableGroupChatsColor() const
{
    return nameColors;
}

void Settings::setExtraColor(const QString& color)
{
    QMutexLocker locker{&bigLock};
    if (color != extraColor) {
        extraColor = color;
        emit extraColorChanged(color);
    }
}

QString Settings::getExtraColor() const
{
    return extraColor;
}

void Settings::setStateExtraColors(int state)
{
    QMutexLocker locker{&bigLock};
    if (state != stateExtraColors) {
        stateExtraColors = state;
        emit StateExtraColorsChanged(state);
    }
}

int Settings::getStateExtraColors() const
{
    return stateExtraColors;
}

/**
 * @brief Write a default personal .ini settings file for a profile.
 * @param basename Filename without extension to save settings.
 *
 * @note If basename is "profile", settings will be saved in profile.ini
 */
void Settings::createPersonal(const QString& basename) const
{
    QMutexLocker locker{&bigLock};

    QString path = paths.getSettingsDirPath() + QDir::separator() + basename + ".ini";
    qDebug() << "Creating new profile settings in " << path;

    QSettings ps(path, QSettings::IniFormat);
    ps.setIniCodec("UTF-8");
    ps.beginGroup("Friends");
    ps.beginWriteArray("Friend", 0);
    ps.endArray();
    ps.endGroup();

    ps.beginGroup("Privacy");
    ps.endGroup();
}

/**
 * @brief Creates a path to the settings dir, if it doesn't already exist
 */
void Settings::createSettingsDir()
{
    QMutexLocker locker{&bigLock};

    QString dir = paths.getSettingsDirPath();
    QDir directory(dir);
    if (!directory.exists() && !directory.mkpath(directory.absolutePath()))
        qCritical() << "Error while creating directory " << dir;
}

/**
 * @brief Waits for all asynchronous operations to complete
 */
void Settings::sync()
{
    if (QThread::currentThread() != settingsThread) {
        QMetaObject::invokeMethod(&getInstance(), "sync", Qt::BlockingQueuedConnection);
        return;
    }

    QMutexLocker locker{&bigLock};
    qApp->processEvents();
}

Settings::friendProp& Settings::getOrInsertFriendPropRef(const ToxPk& id)
{
    // No mutex lock, this is a private fn that should only be called by other
    // public functions that already locked the mutex
    auto it = friendLst.find(id.getByteArray());
    if (it == friendLst.end()) {
        it = friendLst.insert(id.getByteArray(), friendProp{id.toString()});
    }

    return *it;
}

ICoreSettings::ProxyType Settings::fixInvalidProxyType(ICoreSettings::ProxyType proxyType)
{
    // Repair uninitialized enum that was saved to settings due to bug (https://github.com/qTox/qTox/issues/5311)
    switch (proxyType) {
    case ICoreSettings::ProxyType::ptNone:
    case ICoreSettings::ProxyType::ptSOCKS5:
    case ICoreSettings::ProxyType::ptHTTP:
        return proxyType;
    default:
        qWarning() << "Repairing invalid ProxyType, UDP will be enabled";
        return ICoreSettings::ProxyType::ptNone;
    }
}

template <typename T>
bool Settings::setVal(T& savedVal, T newVal) {
    QMutexLocker locker{&bigLock};
    if (savedVal != newVal) {
        savedVal = newVal;
        return true;
    }
    return false;
}
