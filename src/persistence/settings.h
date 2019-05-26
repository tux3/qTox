/*
    Copyright (C) 2013 by Maxim Biro <nurupo.contributions@gmail.com>
    Copyright © 2014-2018 by The qTox Project Contributors

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

#include "src/audio/iaudiosettings.h"
#include "src/core/icoresettings.h"
#include "src/core/toxencrypt.h"
#include "src/core/toxfile.h"
#include "src/persistence/ifriendsettings.h"
#include "src/video/ivideosettings.h"

#include <QDateTime>
#include <QFlags>
#include <QFont>
#include <QHash>
#include <QMutex>
#include <QNetworkProxy>
#include <QObject>
#include <QPixmap>

class Profile;

namespace Db {
enum class syncType;
}

class Settings : public QObject,
                 public ICoreSettings,
                 public IFriendSettings,
                 public IAudioSettings,
                 public IVideoSettings
{
    Q_OBJECT

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
    Q_PROPERTY(bool showIdenticons READ getShowIdenticons WRITE setShowIdenticons NOTIFY
                   showIdenticonsChanged FINAL)

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
    Q_PROPERTY(bool spellCheckingEnabled READ getSpellCheckingEnabled WRITE setSpellCheckingEnabled
                   NOTIFY spellCheckingEnabledChanged FINAL)

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
    Q_PROPERTY(qreal audioThreshold READ getAudioThreshold WRITE setAudioThreshold NOTIFY
                   audioThresholdChanged FINAL)
    Q_PROPERTY(QString outDev READ getOutDev WRITE setOutDev NOTIFY outDevChanged FINAL)
    Q_PROPERTY(bool audioOutDevEnabled READ getAudioOutDevEnabled WRITE setAudioOutDevEnabled NOTIFY
                   audioOutDevEnabledChanged FINAL)
    Q_PROPERTY(int outVolume READ getOutVolume WRITE setOutVolume NOTIFY outVolumeChanged FINAL)
    Q_PROPERTY(int audioBitrate READ getAudioBitrate WRITE setAudioBitrate NOTIFY audioBitrateChanged FINAL)
    Q_PROPERTY(bool enableBackend2 READ getEnableBackend2 WRITE setEnableBackend2 NOTIFY
                   enableBackend2Changed FINAL)

    // Video
    Q_PROPERTY(QString videoDev READ getVideoDev WRITE setVideoDev NOTIFY videoDevChanged FINAL)
    Q_PROPERTY(QRect camVideoRes READ getCamVideoRes WRITE setCamVideoRes NOTIFY camVideoResChanged FINAL)
    Q_PROPERTY(QRect screenRegion READ getScreenRegion WRITE setScreenRegion NOTIFY screenRegionChanged FINAL)
    Q_PROPERTY(bool screenGrabbed READ getScreenGrabbed WRITE setScreenGrabbed NOTIFY screenGrabbedChanged FINAL)
    Q_PROPERTY(float camVideoFPS READ getCamVideoFPS WRITE setCamVideoFPS NOTIFY camVideoFPSChanged FINAL)

public:
    enum class StyleType
    {
        NONE = 0,
        WITH_CHARS = 1,
        WITHOUT_CHARS = 2
    };

    enum class FriendListSortingMode
    {
        Name,
        Activity,
    };

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
    void loadPersonal(QString profileName, const ToxEncrypt* passKey);

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
    void autorunChanged(bool enabled);
    void autoSaveEnabledChanged(bool enabled);
    void autostartInTrayChanged(bool enabled);
    void closeToTrayChanged(bool enabled);
    void lightTrayIconChanged(bool enabled);
    void minimizeToTrayChanged(bool enabled);
    void notifyChanged(bool enabled);
    void desktopNotifyChanged(bool enabled);
    void showWindowChanged(bool enabled);
    void makeToxPortableChanged(bool enabled);
    void busySoundChanged(bool enabled);
    void notifySoundChanged(bool enabled);
    void notifyHideChanged(bool enabled);
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
    void autoAcceptMaxSizeChanged(size_t size);
    void checkUpdatesChanged(bool enabled);
    void widgetDataChanged(const QString& key);

    // GUI
    void autoLoginChanged(bool enabled);
    void nameColorsChanged(bool enabled);
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
    void sortingModeChanged(FriendListSortingMode mode);
    void showIdenticonsChanged(bool enabled);

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
    void spellCheckingEnabledChanged(bool enabled);

    // Privacy
    void typingNotificationChanged(bool enabled);
    void dbSyncTypeChanged(Db::syncType type);
    void blackListChanged(QStringList& blist);

public:
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

    // ICoreSettings
    bool getEnableIPv6() const override;
    void setEnableIPv6(bool enabled) override;

    bool getForceTCP() const override;
    void setForceTCP(bool enabled) override;

    bool getEnableLanDiscovery() const override;
    void setEnableLanDiscovery(bool enabled) override;

    QString getProxyAddr() const override;
    void setProxyAddr(const QString& address) override;

    ICoreSettings::ProxyType getProxyType() const override;
    void setProxyType(ICoreSettings::ProxyType type) override;

    quint16 getProxyPort() const override;
    void setProxyPort(quint16 port) override;

    QNetworkProxy getProxy() const override;

    SIGNAL_IMPL(Settings, enableIPv6Changed, bool enabled)
    SIGNAL_IMPL(Settings, forceTCPChanged, bool enabled)
    SIGNAL_IMPL(Settings, enableLanDiscoveryChanged, bool enabled)
    SIGNAL_IMPL(Settings, proxyTypeChanged, ICoreSettings::ProxyType type)
    SIGNAL_IMPL(Settings, proxyAddressChanged, const QString& address)
    SIGNAL_IMPL(Settings, proxyPortChanged, quint16 port)

    bool getEnableLogging() const;
    void setEnableLogging(bool newValue);

    Db::syncType getDbSyncType() const;
    void setDbSyncType(Db::syncType newValue);

    int getAutoAwayTime() const;
    void setAutoAwayTime(int newValue);

    bool getCheckUpdates() const;
    void setCheckUpdates(bool newValue);

    bool getNotify() const;
    void setNotify(bool newValue);

    bool getShowWindow() const;
    void setShowWindow(bool newValue);

    bool getDesktopNotify() const;
    void setDesktopNotify(bool enabled);

    bool getNotifySound() const;
    void setNotifySound(bool newValue);

    bool getNotifyHide() const;
    void setNotifyHide(bool newValue);

    bool getBusySound() const;
    void setBusySound(bool newValue);

    bool getGroupAlwaysNotify() const;
    void setGroupAlwaysNotify(bool newValue);

    QString getInDev() const override;
    void setInDev(const QString& deviceSpecifier) override;

    bool getAudioInDevEnabled() const override;
    void setAudioInDevEnabled(bool enabled) override;

    QString getOutDev() const override;
    void setOutDev(const QString& deviceSpecifier) override;

    bool getAudioOutDevEnabled() const override;
    void setAudioOutDevEnabled(bool enabled) override;

    qreal getAudioInGainDecibel() const override;
    void setAudioInGainDecibel(qreal dB) override;

    qreal getAudioThreshold() const override;
    void setAudioThreshold(qreal percent) override;

    int getOutVolume() const override;
    int getOutVolumeMin() const override
    {
        return 0;
    }
    int getOutVolumeMax() const override
    {
        return 100;
    }
    void setOutVolume(int volume) override;

    int getAudioBitrate() const override;
    void setAudioBitrate(int bitrate) override;

    bool getEnableTestSound() const override;
    void setEnableTestSound(bool newValue) override;

    bool getEnableBackend2() const override;
    void setEnableBackend2(bool enabled) override;

    SIGNAL_IMPL(Settings, inDevChanged, const QString& device)
    SIGNAL_IMPL(Settings, audioInDevEnabledChanged, bool enabled)

    SIGNAL_IMPL(Settings, outDevChanged, const QString& device)
    SIGNAL_IMPL(Settings, audioOutDevEnabledChanged, bool enabled)

    SIGNAL_IMPL(Settings, audioInGainDecibelChanged, qreal dB)
    SIGNAL_IMPL(Settings, audioThresholdChanged, qreal percent)
    SIGNAL_IMPL(Settings, outVolumeChanged, int volume)
    SIGNAL_IMPL(Settings, audioBitrateChanged, int bitrate)
    SIGNAL_IMPL(Settings, enableTestSoundChanged, bool newValue)
    SIGNAL_IMPL(Settings, enableBackend2Changed, bool enabled)

    QString getVideoDev() const override;
    void setVideoDev(const QString& deviceSpecifier) override;

    QRect getScreenRegion() const override;
    void setScreenRegion(const QRect& value) override;

    bool getScreenGrabbed() const override;
    void setScreenGrabbed(bool value) override;

    QRect getCamVideoRes() const override;
    void setCamVideoRes(QRect newValue) override;

    float getCamVideoFPS() const override;
    void setCamVideoFPS(float newValue) override;

    SIGNAL_IMPL(Settings, videoDevChanged, const QString& device)
    SIGNAL_IMPL(Settings, screenRegionChanged, const QRect& region)
    SIGNAL_IMPL(Settings, screenGrabbedChanged, bool enabled)
    SIGNAL_IMPL(Settings, camVideoResChanged, const QRect& region)
    SIGNAL_IMPL(Settings, camVideoFPSChanged, unsigned short fps)

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

    QString getContactNote(const ToxPk& id) const override;
    void setContactNote(const ToxPk& id, const QString& note) override;

    QString getAutoAcceptDir(const ToxPk& id) const override;
    void setAutoAcceptDir(const ToxPk& id, const QString& dir) override;

    AutoAcceptCallFlags getAutoAcceptCall(const ToxPk& id) const override;
    void setAutoAcceptCall(const ToxPk& id, AutoAcceptCallFlags accept) override;

    QString getGlobalAutoAcceptDir() const;
    void setGlobalAutoAcceptDir(const QString& dir);

    size_t getMaxAutoAcceptSize() const;
    void setMaxAutoAcceptSize(size_t size);

    bool getAutoGroupInvite(const ToxPk& id) const override;
    void setAutoGroupInvite(const ToxPk& id, bool accept) override;

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

    bool getSpellCheckingEnabled() const;
    void setSpellCheckingEnabled(bool newValue);

    // Privacy
    bool getTypingNotification() const;
    void setTypingNotification(bool enabled);
    QStringList getBlackList() const;
    void setBlackList(const QStringList& blist);

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

    QString getFriendAlias(const ToxPk& id) const override;
    void setFriendAlias(const ToxPk& id, const QString& alias) override;

    int getFriendCircleID(const ToxPk& id) const override;
    void setFriendCircleID(const ToxPk& id, int circleID) override;

    QDateTime getFriendActivity(const ToxPk& id) const override;
    void setFriendActivity(const ToxPk& id, const QDateTime& date) override;

    void saveFriendSettings(const ToxPk& id) override;
    void removeFriendSettings(const ToxPk& id) override;

    SIGNAL_IMPL(Settings, autoAcceptCallChanged, const ToxPk& id,
                IFriendSettings::AutoAcceptCallFlags accept)
    SIGNAL_IMPL(Settings, autoGroupInviteChanged, const ToxPk& id, bool accept)
    SIGNAL_IMPL(Settings, autoAcceptDirChanged, const ToxPk& id, const QString& dir)
    SIGNAL_IMPL(Settings, contactNoteChanged, const ToxPk& id, const QString& note)

    bool getCompactLayout() const;
    void setCompactLayout(bool compact);

    FriendListSortingMode getFriendSortingMode() const;
    void setFriendSortingMode(FriendListSortingMode mode);

    bool getSeparateWindow() const;
    void setSeparateWindow(bool value);

    bool getDontGroupWindows() const;
    void setDontGroupWindows(bool value);

    bool getGroupchatPosition() const;
    void setGroupchatPosition(bool value);

    bool getShowIdenticons() const;
    void setShowIdenticons(bool value);

    bool getAutoLogin() const;
    void setAutoLogin(bool state);
    void setEnableGroupChatsColor(bool state);
    bool getEnableGroupChatsColor() const;

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
    struct friendProp;

    Settings();
    ~Settings();
    Settings(Settings& settings) = delete;
    Settings& operator=(const Settings&) = delete;
    void savePersonal(QString profileName, const ToxEncrypt* passkey);
    friendProp& getOrInsertFriendPropRef(const ToxPk& id);
    ICoreSettings::ProxyType fixInvalidProxyType(ICoreSettings::ProxyType proxyType);

public slots:
    void savePersonal(Profile* profile);

private:
    bool loaded;

    bool useCustomDhtList;
    int dhtServerId;
    bool dontShowDhtDialog;

    bool autoLogin;
    bool compactLayout;
    FriendListSortingMode sortingMode;
    bool groupchatPosition;
    bool separateWindow;
    bool dontGroupWindows;
    bool showIdenticons;
    bool enableIPv6;
    QString translation;
    bool makeToxPortable;
    bool autostartInTray;
    bool closeToTray;
    bool minimizeToTray;
    bool lightTrayIcon;
    bool useEmoticons;
    bool checkUpdates;
    bool notify;
    bool desktopNotify;
    bool showWindow;
    bool notifySound;
    bool notifyHide;
    bool busySound;
    bool groupAlwaysNotify;
    bool nameColors;

    bool forceTCP;
    bool enableLanDiscovery;

    ICoreSettings::ProxyType proxyType;
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
    size_t autoAcceptMaxSize;

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
    bool spellCheckingEnabled;

    // Privacy
    bool typingNotification;
    Db::syncType dbSyncType;
    QStringList blackList;

    // Audio
    QString inDev;
    bool audioInDevEnabled;
    qreal audioInGainDecibel;
    qreal audioThreshold;
    QString outDev;
    bool audioOutDevEnabled;
    int outVolume;
    int audioBitrate;
    bool enableTestSound;
    bool enableBackend2;

    // Video
    QString videoDev;
    QRect camVideoRes;
    QRect screenRegion;
    bool screenGrabbed;
    float camVideoFPS;

    struct friendProp
    {
        friendProp() = delete;
        friendProp(QString addr)
            : addr(addr)
        {}
        QString alias = "";
        QString addr = "";
        QString autoAcceptDir = "";
        QString note = "";
        int circleID = -1;
        QDateTime activity = QDateTime();
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

#endif // SETTINGS_HPP
