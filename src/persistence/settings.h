/*
    Copyright (C) 2013 by Maxim Biro <nurupo.contributions@gmail.com>
    Copyright © 2014-2015 by The qTox Project Contributors

    This file is part of qTox, a Qt-based graphical interface for Tox.

    This program is free software: you can redistribute it and/or modify
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

#ifndef SETTINGS_HPP
#define SETTINGS_HPP

#include "src/core/corestructs.h"
#include "src/core/toxencrypt.h"
#include <QDate>
#include <QFlags>
#include <QFont>
#include <QHash>
#include <QMutex>
#include <QNetworkProxy>
#include <QObject>
#include <QPixmap>

class ToxPk;
class Profile;

namespace Db {
enum class syncType;
}

class Settings : public QObject
{
    Q_OBJECT

    Q_ENUMS(ProxyType)
    Q_ENUMS(StyleType)

    // general
    Q_PROPERTY(bool compactLayout READ getCompactLayout WRITE setCompactLayout NOTIFY compactLayoutChanged FINAL)
    Q_PROPERTY(bool autorun READ getAutorun WRITE setAutorun NOTIFY autorunChanged FINAL)

    // GUI
    Q_PROPERTY(bool separateWindow READ getSeparateWindow WRITE setSeparateWindow NOTIFY
                   separateWindowChanged FINAL)
    Q_PROPERTY(QString smileyPack READ getSmileyPack WRITE setSmileyPack NOTIFY smileyPackChanged FINAL)
    Q_PROPERTY(int emojiFontPointSize READ getEmojiFontPointSize WRITE setEmojiFontPointSize NOTIFY
                   emojiFontPointSizeChanged FINAL)
    Q_PROPERTY(bool minimizeOnClose READ getMinimizeOnClose WRITE setMinimizeOnClose NOTIFY
                   minimizeOnCloseChanged FINAL)
    Q_PROPERTY(QByteArray windowGeometry READ getWindowGeometry WRITE setWindowGeometry NOTIFY
                   windowGeometryChanged FINAL)
    Q_PROPERTY(QByteArray windowState READ getWindowState WRITE setWindowState NOTIFY windowStateChanged FINAL)
    Q_PROPERTY(QByteArray splitterState READ getSplitterState WRITE setSplitterState NOTIFY
                   splitterStateChanged FINAL)
    Q_PROPERTY(QByteArray dialogGeometry READ getDialogGeometry WRITE setDialogGeometry NOTIFY
                   dialogGeometryChanged FINAL)
    Q_PROPERTY(QByteArray dialogSplitterState READ getDialogSplitterState WRITE
                   setDialogSplitterState NOTIFY dialogSplitterStateChanged FINAL)
    Q_PROPERTY(QByteArray dialogSettingsGeometry READ getDialogSettingsGeometry WRITE
                   setDialogSettingsGeometry NOTIFY dialogSettingsGeometryChanged FINAL)
    Q_PROPERTY(QString style READ getStyle WRITE setStyle NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool showSystemTray READ getShowSystemTray WRITE setShowSystemTray NOTIFY
                   showSystemTrayChanged FINAL)

    // ChatView
    Q_PROPERTY(bool groupchatPosition READ getGroupchatPosition WRITE setGroupchatPosition NOTIFY
                   groupchatPositionChanged FINAL)
    Q_PROPERTY(QFont chatMessageFont READ getChatMessageFont WRITE setChatMessageFont NOTIFY
                   chatMessageFontChanged FINAL)
    Q_PROPERTY(StyleType stylePreference READ getStylePreference WRITE setStylePreference NOTIFY
                   stylePreferenceChanged FINAL)
    Q_PROPERTY(QString timestampFormat READ getTimestampFormat WRITE setTimestampFormat NOTIFY
                   timestampFormatChanged FINAL)
    Q_PROPERTY(QString dateFormat READ getDateFormat WRITE setDateFormat NOTIFY dateFormatChanged FINAL)
    Q_PROPERTY(bool statusChangeNotificationEnabled READ getStatusChangeNotificationEnabled WRITE
                   setStatusChangeNotificationEnabled NOTIFY statusChangeNotificationEnabledChanged FINAL)

    // Privacy
    Q_PROPERTY(bool typingNotification READ getTypingNotification WRITE setTypingNotification NOTIFY
                   typingNotificationChanged FINAL)
    Q_PROPERTY(QStringList blackList READ getBlackList WRITE setBlackList NOTIFY blackListChanged FINAL)

    // Audio
    Q_PROPERTY(QString inDev READ getInDev WRITE setInDev NOTIFY inDevChanged FINAL)
    Q_PROPERTY(bool audioInDevEnabled READ getAudioInDevEnabled WRITE setAudioInDevEnabled NOTIFY
                   audioInDevEnabledChanged FINAL)
    Q_PROPERTY(qreal audioInGainDecibel READ getAudioInGainDecibel WRITE setAudioInGainDecibel
                   NOTIFY audioInGainDecibelChanged FINAL)
    Q_PROPERTY(QString outDev READ getOutDev WRITE setOutDev NOTIFY outDevChanged FINAL)
    Q_PROPERTY(bool audioOutDevEnabled READ getAudioOutDevEnabled WRITE setAudioOutDevEnabled NOTIFY
                   audioOutDevEnabledChanged FINAL)
    Q_PROPERTY(int outVolume READ getOutVolume WRITE setOutVolume NOTIFY outVolumeChanged FINAL)
    Q_PROPERTY(bool enableBackend2 READ getEnableBackend2 WRITE setEnableBackend2 NOTIFY
                   enableBackend2Changed FINAL)

    // Video
    Q_PROPERTY(QString videoDev READ getVideoDev WRITE setVideoDev NOTIFY videoDevChanged FINAL)
    Q_PROPERTY(QRect camVideoRes READ getCamVideoRes WRITE setCamVideoRes NOTIFY camVideoResChanged FINAL)
    Q_PROPERTY(QRect screenRegion READ getScreenRegion WRITE setScreenRegion NOTIFY screenRegionChanged FINAL)
    Q_PROPERTY(bool screenGrabbed READ getScreenGrabbed WRITE setScreenGrabbed NOTIFY screenGrabbedChanged FINAL)
    Q_PROPERTY(quint16 camVideoFPS READ getCamVideoFPS WRITE setCamVideoFPS NOTIFY camVideoFPSChanged FINAL)

public:
    enum class ProxyType
    {
        ptNone = 0,
        ptSOCKS5 = 1,
        ptHTTP = 2
    };
    enum class StyleType
    {
        NONE = 0,
        WITH_CHARS = 1,
        WITHOUT_CHARS = 2
    };
    enum class AutoAcceptCall
    {
        None = 0x00,
        Audio = 0x01,
        Video = 0x02,
        AV = Audio | Video
    };
    Q_DECLARE_FLAGS(AutoAcceptCallFlags, AutoAcceptCall)

public:
    static Settings& getInstance();
    static void destroyInstance();
    QString getSettingsDirPath() const;
    QString getAppDataDirPath() const;
    QString getAppCacheDirPath() const;

    void createSettingsDir();
    void createPersonal(QString basename);

    void savePersonal();

    void loadGlobal();
    void loadPersonal();
    void loadPersonal(Profile* profile);

    void resetToDefault();

    struct Request
    {
        QString address;
        QString message;
        bool read;
    };

public slots:
    void saveGlobal();
    void sync();

signals:
    // General
    void enableIPv6Changed(bool enabled);
    void forceTCPChanged(bool enabled);
    void proxyTypeChanged(ProxyType type);
    void proxyAddressChanged(const QString& address);
    void proxyPortChanged(quint16 port);
    void dhtServerListChanged(const QList<DhtServer>& servers);
    void autorunChanged(bool enabled);
    void autoSaveEnabledChanged(bool enabled);
    void autostartInTrayChanged(bool enabled);
    void showInFrontChanged(bool enabled);
    void closeToTrayChanged(bool enabled);
    void lightTrayIconChanged(bool enabled);
    void minimizeToTrayChanged(bool enabled);
    void showWindowChanged(bool enabled);
    void makeToxPortableChanged(bool enabled);
    void busySoundChanged(bool enabled);
    void notifySoundChanged(bool enabled);
    void groupAlwaysNotifyChanged(bool enabled);
    void translationChanged(const QString& translation);
    void toxmeInfoChanged(const QString& info);
    void toxmeBioChanged(const QString& bio);
    void toxmePrivChanged(bool priv);
    void toxmePassChanged();
    void currentProfileChanged(const QString& profile);
    void currentProfileIdChanged(quint32 id);
    void enableLoggingChanged(bool enabled);
    void autoAwayTimeChanged(int minutes);
    void globalAutoAcceptDirChanged(const QString& path);
    void checkUpdatesChanged(bool enabled);
    void widgetDataChanged(const QString& key);
    void autoAcceptCallChanged(const ToxPk& id, AutoAcceptCallFlags accept);
    void autoGroupInviteChanged(const ToxPk& id, bool accept);

    // GUI
    void autoLoginChanged(bool enabled);
    void separateWindowChanged(bool enabled);
    void showSystemTrayChanged(bool enabled);
    bool minimizeOnCloseChanged(bool enabled);
    void windowGeometryChanged(const QByteArray& rect);
    void windowStateChanged(const QByteArray& state);
    void splitterStateChanged(const QByteArray& state);
    void dialogGeometryChanged(const QByteArray& rect);
    void dialogSplitterStateChanged(const QByteArray& state);
    void dialogSettingsGeometryChanged(const QByteArray& rect);
    void styleChanged(const QString& style);
    void themeColorChanged(int color);
    void compactLayoutChanged(bool enabled);

    // ChatView
    void useEmoticonsChanged(bool enabled);
    void smileyPackChanged(const QString& name);
    void emojiFontPointSizeChanged(int size);
    void dontGroupWindowsChanged(bool enabled);
    void groupchatPositionChanged(bool enabled);
    void chatMessageFontChanged(const QFont& font);
    void stylePreferenceChanged(StyleType type);
    void timestampFormatChanged(const QString& format);
    void dateFormatChanged(const QString& format);
    void statusChangeNotificationEnabledChanged(bool enabled);
    void fauxOfflineMessagingChanged(bool enabled);

    // Privacy
    void typingNotificationChanged(bool enabled);
    void dbSyncTypeChanged(Db::syncType type);
    void blackListChanged(QStringList& blist);

    // Audio
    void inDevChanged(const QString& name);
    void audioInDevEnabledChanged(bool enabled);
    void audioInGainDecibelChanged(qreal gain);
    void outDevChanged(const QString& name);
    void audioOutDevEnabledChanged(bool enabled);
    void outVolumeChanged(int volume);
    void enableTestSoundChanged(bool enabled);
    void enableBackend2Changed(bool enabled);

    // Video
    void videoDevChanged(const QString& name);
    void camVideoResChanged(const QRect& resolution);
    void screenRegionChanged(const QRect& region);
    void screenGrabbedChanged(bool enabled);
    void camVideoFPSChanged(quint16 fps);

public:
    const QList<DhtServer>& getDhtServerList() const;
    void setDhtServerList(const QList<DhtServer>& newDhtServerList);

    bool getEnableIPv6() const;
    void setEnableIPv6(bool newValue);

    bool getMakeToxPortable() const;
    void setMakeToxPortable(bool newValue);

    bool getAutorun() const;
    void setAutorun(bool newValue);

    bool getAutostartInTray() const;
    void setAutostartInTray(bool newValue);

    bool getCloseToTray() const;
    void setCloseToTray(bool newValue);

    bool getMinimizeToTray() const;
    void setMinimizeToTray(bool newValue);

    bool getLightTrayIcon() const;
    void setLightTrayIcon(bool newValue);

    QString getStyle() const;
    void setStyle(const QString& newValue);

    bool getShowSystemTray() const;
    void setShowSystemTray(bool newValue);

    bool getUseEmoticons() const;
    void setUseEmoticons(bool newValue);

    QString getCurrentProfile() const;
    uint32_t getCurrentProfileId() const;
    void setCurrentProfile(const QString& profile);

    QString getTranslation() const;
    void setTranslation(const QString& newValue);

    // Toxme
    void deleteToxme();
    void setToxme(QString name, QString server, QString bio, bool priv, QString pass = "");
    QString getToxmeInfo() const;
    void setToxmeInfo(const QString& info);

    QString getToxmeBio() const;
    void setToxmeBio(const QString& bio);

    bool getToxmePriv() const;
    void setToxmePriv(bool priv);

    QString getToxmePass() const;
    void setToxmePass(const QString& pass);

    void setAutoSaveEnabled(bool newValue);
    bool getAutoSaveEnabled() const;

    bool getForceTCP() const;
    void setForceTCP(bool newValue);

    QNetworkProxy getProxy() const;

    QString getProxyAddr() const;
    void setProxyAddr(const QString& newValue);

    ProxyType getProxyType() const;
    void setProxyType(ProxyType newValue);

    quint16 getProxyPort() const;
    void setProxyPort(quint16 newValue);

    bool getEnableLogging() const;
    void setEnableLogging(bool newValue);

    Db::syncType getDbSyncType() const;
    void setDbSyncType(Db::syncType newValue);

    int getAutoAwayTime() const;
    void setAutoAwayTime(int newValue);

    bool getCheckUpdates() const;
    void setCheckUpdates(bool newValue);

    bool getShowWindow() const;
    void setShowWindow(bool newValue);

    bool getShowInFront() const;
    void setShowInFront(bool newValue);

    bool getNotifySound() const;
    void setNotifySound(bool newValue);

    bool getBusySound() const;
    void setBusySound(bool newValue);

    bool getGroupAlwaysNotify() const;
    void setGroupAlwaysNotify(bool newValue);

    QString getInDev() const;
    void setInDev(const QString& deviceSpecifier);

    bool getAudioInDevEnabled() const;
    void setAudioInDevEnabled(bool enabled);

    QString getOutDev() const;
    void setOutDev(const QString& deviceSpecifier);

    bool getAudioOutDevEnabled() const;
    void setAudioOutDevEnabled(bool enabled);

    qreal getAudioInGainDecibel() const;
    void setAudioInGainDecibel(qreal dB);

    int getOutVolume() const;
    void setOutVolume(int volume);

    bool getEnableTestSound() const;
    void setEnableTestSound(bool newValue);

    bool getEnableBackend2() const;
    void setEnableBackend2(bool enabled);

    QString getVideoDev() const;
    void setVideoDev(const QString& deviceSpecifier);

    QRect getScreenRegion() const;
    void setScreenRegion(const QRect& value);

    bool getScreenGrabbed() const;
    void setScreenGrabbed(bool value);

    QRect getCamVideoRes() const;
    void setCamVideoRes(QRect newValue);

    unsigned short getCamVideoFPS() const;
    void setCamVideoFPS(unsigned short newValue);

    bool isAnimationEnabled() const;
    void setAnimationEnabled(bool newValue);

    QString getSmileyPack() const;
    void setSmileyPack(const QString& value);

    int getThemeColor() const;
    void setThemeColor(int value);

    StyleType getStylePreference() const;
    void setStylePreference(StyleType newValue);

    bool isCurstomEmojiFont() const;
    void setCurstomEmojiFont(bool value);

    int getEmojiFontPointSize() const;
    void setEmojiFontPointSize(int value);

    QString getContactNote(const ToxPk& id) const;
    void setContactNote(const ToxPk& id, const QString& note);

    QString getAutoAcceptDir(const ToxPk& id) const;
    void setAutoAcceptDir(const ToxPk& id, const QString& dir);

    AutoAcceptCallFlags getAutoAcceptCall(const ToxPk& id) const;
    void setAutoAcceptCall(const ToxPk& id, AutoAcceptCallFlags accept);

    QString getGlobalAutoAcceptDir() const;
    void setGlobalAutoAcceptDir(const QString& dir);

    bool getAutoGroupInvite(const ToxPk& id) const;
    void setAutoGroupInvite(const ToxPk& id, bool accept);

    // ChatView
    const QFont& getChatMessageFont() const;
    void setChatMessageFont(const QFont& font);

    const QString& getTimestampFormat() const;
    void setTimestampFormat(const QString& format);

    const QString& getDateFormat() const;
    void setDateFormat(const QString& format);

    bool getMinimizeOnClose() const;
    void setMinimizeOnClose(bool newValue);

    bool getStatusChangeNotificationEnabled() const;
    void setStatusChangeNotificationEnabled(bool newValue);

    // Privacy
    bool getTypingNotification() const;
    void setTypingNotification(bool enabled);
    QStringList getBlackList() const;
    void setBlackList(QStringList& blist);

    // State
    QByteArray getWindowGeometry() const;
    void setWindowGeometry(const QByteArray& value);

    QByteArray getWindowState() const;
    void setWindowState(const QByteArray& value);

    QByteArray getSplitterState() const;
    void setSplitterState(const QByteArray& value);

    QByteArray getDialogGeometry() const;
    void setDialogGeometry(const QByteArray& value);

    QByteArray getDialogSplitterState() const;
    void setDialogSplitterState(const QByteArray& value);

    QByteArray getDialogSettingsGeometry() const;
    void setDialogSettingsGeometry(const QByteArray& value);

    QString getFriendAddress(const QString& publicKey) const;
    void updateFriendAddress(const QString& newAddr);

    QString getFriendAlias(const ToxPk& id) const;
    void setFriendAlias(const ToxPk& id, const QString& alias);

    int getFriendCircleID(const ToxPk& id) const;
    void setFriendCircleID(const ToxPk& id, int circleID);

    QDate getFriendActivity(const ToxPk& id) const;
    void setFriendActivity(const ToxPk& id, const QDate& date);

    void removeFriendSettings(const ToxPk& id);

    bool getFauxOfflineMessaging() const;
    void setFauxOfflineMessaging(bool value);

    bool getCompactLayout() const;
    void setCompactLayout(bool compact);

    bool getSeparateWindow() const;
    void setSeparateWindow(bool value);

    bool getDontGroupWindows() const;
    void setDontGroupWindows(bool value);

    bool getGroupchatPosition() const;
    void setGroupchatPosition(bool value);

    bool getAutoLogin() const;
    void setAutoLogin(bool state);

    int getCircleCount() const;
    int addCircle(const QString& name = QString());
    int removeCircle(int id);
    QString getCircleName(int id) const;
    void setCircleName(int id, const QString& name);
    bool getCircleExpanded(int id) const;
    void setCircleExpanded(int id, bool expanded);

    bool addFriendRequest(const QString& friendAddress, const QString& message);
    unsigned int getUnreadFriendRequests() const;
    Request getFriendRequest(int index) const;
    int getFriendRequestSize() const;
    void clearUnreadFriendRequests();
    void removeFriendRequest(int index);
    void readFriendRequest(int index);

    QByteArray getWidgetData(const QString& uniqueName) const;
    void setWidgetData(const QString& uniqueName, const QByteArray& data);

    // Wrappers around getWidgetData() and setWidgetData()
    // Assume widget has a unique objectName set
    template <class T>
    void restoreGeometryState(T* widget) const
    {
        widget->restoreGeometry(getWidgetData(widget->objectName() + "Geometry"));
        widget->restoreState(getWidgetData(widget->objectName() + "State"));
    }
    template <class T>
    void saveGeometryState(const T* widget)
    {
        setWidgetData(widget->objectName() + "Geometry", widget->saveGeometry());
        setWidgetData(widget->objectName() + "State", widget->saveState());
    }

    static uint32_t makeProfileId(const QString& profile);

private:
    Settings();
    ~Settings();
    Settings(Settings& settings) = delete;
    Settings& operator=(const Settings&) = delete;
    void savePersonal(QString profileName, const ToxEncrypt* passkey);

public slots:
    void savePersonal(Profile* profile);

private:
    bool loaded;

    bool useCustomDhtList;
    QList<DhtServer> dhtServerList;
    int dhtServerId;
    bool dontShowDhtDialog;

    bool autoLogin;
    bool fauxOfflineMessaging;
    bool compactLayout;
    bool groupchatPosition;
    bool separateWindow;
    bool dontGroupWindows;
    bool enableIPv6;
    QString translation;
    bool makeToxPortable;
    bool autostartInTray;
    bool closeToTray;
    bool minimizeToTray;
    bool lightTrayIcon;
    bool useEmoticons;
    bool checkUpdates;
    bool showWindow;
    bool showInFront;
    bool notifySound;
    bool busySound;
    bool groupAlwaysNotify;

    bool forceTCP;

    ProxyType proxyType;
    QString proxyAddr;
    quint16 proxyPort;

    QString currentProfile;
    uint32_t currentProfileId;

    // Toxme Info
    QString toxmeInfo;
    QString toxmeBio;
    bool toxmePriv;
    QString toxmePass;

    bool enableLogging;

    int autoAwayTime;

    QHash<QString, QByteArray> widgetSettings;
    QHash<QString, QString> autoAccept;
    bool autoSaveEnabled;
    QString globalAutoAcceptDir;

    QList<Request> friendRequests;

    // GUI
    QString smileyPack;
    int emojiFontPointSize;
    bool minimizeOnClose;
    QByteArray windowGeometry;
    QByteArray windowState;
    QByteArray splitterState;
    QByteArray dialogGeometry;
    QByteArray dialogSplitterState;
    QByteArray dialogSettingsGeometry;
    QString style;
    bool showSystemTray;

    // ChatView
    QFont chatMessageFont;
    StyleType stylePreference;
    int firstColumnHandlePos;
    int secondColumnHandlePosFromRight;
    QString timestampFormat;
    QString dateFormat;
    bool statusChangeNotificationEnabled;

    // Privacy
    bool typingNotification;
    Db::syncType dbSyncType;
    QStringList blackList;

    // Audio
    QString inDev;
    bool audioInDevEnabled;
    qreal audioInGainDecibel;
    QString outDev;
    bool audioOutDevEnabled;
    int outVolume;
    bool enableTestSound;
    bool enableBackend2;

    // Video
    QString videoDev;
    QRect camVideoRes;
    QRect screenRegion;
    bool screenGrabbed;
    unsigned short camVideoFPS;

    struct friendProp
    {
        QString alias;
        QString addr;
        QString autoAcceptDir;
        QString note;
        int circleID = -1;
        QDate activity = QDate();
        AutoAcceptCallFlags autoAcceptCall;
        bool autoGroupInvite = false;
    };

    struct circleProp
    {
        QString name;
        bool expanded;
    };

    QHash<QByteArray, friendProp> friendLst;

    QVector<circleProp> circleLst;

    int themeColor;

    static QMutex bigLock;
    static Settings* settings;
    static const QString globalSettingsFile;
    static QThread* settingsThread;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Settings::AutoAcceptCallFlags)
#endif // SETTINGS_HPP
