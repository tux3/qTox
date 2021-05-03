/*
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

#pragma once

#include "ui_mainwindow.h"

#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>
#include <QSystemTrayIcon>

#include "genericchatitemwidget.h"

#include "audio/iaudiocontrol.h"
#include "audio/iaudiosink.h"
#include "src/core/core.h"
#include "src/core/groupid.h"
#include "src/core/toxfile.h"
#include "src/core/toxid.h"
#include "src/core/toxpk.h"
#include "src/model/friendmessagedispatcher.h"
#include "src/model/groupmessagedispatcher.h"
#if DESKTOP_NOTIFICATIONS
#include "src/model/notificationgenerator.h"
#include "src/platform/desktop_notifications/desktopnotify.h"
#endif

#define PIXELS_TO_ACT 7

namespace Ui {
class MainWindow;
}

class AddFriendForm;
class AlSink;
class Camera;
class ChatForm;
class CircleWidget;
class ContentDialog;
class ContentLayout;
class Core;
class FilesForm;
class Friend;
class FriendChatroom;
class FriendListWidget;
class FriendWidget;
class GenericChatroomWidget;
class Group;
class GroupChatForm;
class GroupChatroom;
class GroupInvite;
class GroupInviteForm;
class GroupWidget;
class MaskablePixmapWidget;
class ProfileForm;
class ProfileInfo;
class QActionGroup;
class QMenu;
class QPushButton;
class QSplitter;
class QTimer;
class SettingsWidget;
class SystemTrayIcon;
class VideoSurface;
class UpdateCheck;
class Settings;
class IChatLog;
class ChatHistory;

class Widget final : public QMainWindow
{
    Q_OBJECT

private:
    enum class ActiveToolMenuButton
    {
        AddButton,
        GroupButton,
        TransferButton,
        SettingButton,
        None,
    };

    enum class DialogType
    {
        AddDialog,
        TransferDialog,
        SettingDialog,
        ProfileDialog,
        GroupDialog
    };

    enum class FilterCriteria
    {
        All = 0,
        Online,
        Offline,
        Friends,
        Groups
    };

public:
    explicit Widget(Profile& _profile, IAudioControl& audio, QWidget* parent = nullptr);
    ~Widget() override;
    void init();
    void setCentralWidget(QWidget* widget, const QString& widgetName);
    QString getUsername();
    Camera* getCamera();
    static Widget* getInstance(IAudioControl* audio = nullptr);
    void showUpdateDownloadProgress();
    void addFriendDialog(const Friend* frnd, ContentDialog* dialog);
    void addGroupDialog(Group* group, ContentDialog* dialog);
    bool newFriendMessageAlert(const ToxPk& friendId, const QString& text, bool sound = true,
                               QString filename = QString(), size_t filesize = 0);
    bool newGroupMessageAlert(const GroupId& groupId, const ToxPk& authorPk, const QString& message,
                              bool notify);
    bool getIsWindowMinimized();
    void updateIcons();

    static QString fromDialogType(DialogType type);
    ContentDialog* createContentDialog() const;
    ContentLayout* createContentDialog(DialogType type) const;

    static void confirmExecutableOpen(const QFileInfo& file);

    void clearAllReceipts();

    static inline QIcon prepareIcon(QString path, int w = 0, int h = 0);

    bool groupsVisible() const;

    void resetIcon();

public slots:
    void reloadTheme();
    void onShowSettings();
    void onSeparateWindowClicked(bool separate);
    void onSeparateWindowChanged(bool separate, bool clicked);
    void setWindowTitle(const QString& title);
    void forceShow();
    void onConnected();
    void onDisconnected();
    void onStatusSet(Status::Status status);
    void onFailedToStartCore();
    void onBadProxyCore();
    void onSelfAvatarLoaded(const QPixmap& pic);
    void setUsername(const QString& username);
    void setStatusMessage(const QString& statusMessage);
    void addFriend(uint32_t friendId, const ToxPk& friendPk);
    void addFriendFailed(const ToxPk& userId, const QString& errorInfo = QString());
    void onCoreFriendStatusChanged(int friendId, Status::Status status);
    void onFriendStatusChanged(const ToxPk& friendPk, Status::Status status);
    void onFriendStatusMessageChanged(int friendId, const QString& message);
    void onFriendDisplayedNameChanged(const QString& displayed);
    void onFriendUsernameChanged(int friendId, const QString& username);
    void onFriendAliasChanged(const ToxPk& friendId, const QString& alias);
    void onFriendMessageReceived(uint32_t friendnumber, const QString& message, bool isAction);
    void onReceiptReceived(int friendId, ReceiptNum receipt);
    void onExtendedMessageSupport(uint32_t friendNumber, bool supported);
    void onFriendExtMessageReceived(uint32_t friendNumber, const QString& message);
    void onExtReceiptReceived(uint32_t friendNumber, uint64_t receiptId);
    void onFriendRequestReceived(const ToxPk& friendPk, const QString& message);
    void onFileReceiveRequested(const ToxFile& file);
    void onEmptyGroupCreated(uint32_t groupnumber, const GroupId& groupId, const QString& title);
    void onGroupJoined(int groupNum, const GroupId& groupId);
    void onGroupInviteReceived(const GroupInvite& inviteInfo);
    void onGroupInviteAccepted(const GroupInvite& inviteInfo);
    void onGroupMessageReceived(int groupnumber, int peernumber, const QString& message, bool isAction);
    void onGroupPeerlistChanged(uint32_t groupnumber);
    void onGroupPeerNameChanged(uint32_t groupnumber, const ToxPk& peerPk, const QString& newName);
    void onGroupTitleChanged(uint32_t groupnumber, const QString& author, const QString& title);
    void titleChangedByUser(const QString& title);
    void onGroupPeerAudioPlaying(int groupnumber, ToxPk peerPk);
    void onGroupSendFailed(uint32_t groupnumber);
    void onFriendTypingChanged(uint32_t friendnumber, bool isTyping);
    void nextContact();
    void previousContact();
    void onFriendDialogShown(const Friend* f);
    void onGroupDialogShown(Group* g);
    void toggleFullscreen();
    void refreshPeerListsLocal(const QString& username);
    void onUpdateAvailable();
    void onCoreChanged(Core& core);

signals:
    void friendRequestAccepted(const ToxPk& friendPk);
    void friendRequested(const ToxId& friendAddress, const QString& message);
    void statusSet(Status::Status status);
    void statusSelected(Status::Status status);
    void usernameChanged(const QString& username);
    void changeGroupTitle(uint32_t groupnumber, const QString& title);
    void statusMessageChanged(const QString& statusMessage);
    void resized();
    void windowStateChanged(Qt::WindowStates states);

private slots:
    void onAddClicked();
    void onGroupClicked();
    void onTransferClicked();
    void showProfile();
    void openNewDialog(GenericChatroomWidget* widget);
    void onChatroomWidgetClicked(GenericChatroomWidget* widget);
    void onStatusMessageChanged(const QString& newStatusMessage);
    void removeFriend(const ToxPk& friendId);
    void copyFriendIdToClipboard(const ToxPk& friendId);
    void removeGroup(const GroupId& groupId);
    void setStatusOnline();
    void setStatusAway();
    void setStatusBusy();
    void onIconClick(QSystemTrayIcon::ActivationReason);
    void onUserAwayCheck();
    void onEventIconTick();
    void onTryCreateTrayIcon();
    void onSetShowSystemTray(bool newValue);
    void onSplitterMoved(int pos, int index);
    void friendListContextMenu(const QPoint& pos);
    void friendRequestsUpdate();
    void groupInvitesUpdate();
    void groupInvitesClear();
    void onDialogShown(GenericChatroomWidget* widget);
    void outgoingNotification();
    void onCallEnd();
    void incomingNotification(uint32_t friendId);
    void onStopNotification();
    void dispatchFile(ToxFile file);
    void dispatchFileWithBool(ToxFile file, bool);
    void dispatchFileSendFailed(uint32_t friendId, const QString& fileName);
    void connectCircleWidget(CircleWidget& circleWidget);
    void connectFriendWidget(FriendWidget& friendWidget);
    void searchCircle(CircleWidget& circleWidget);
    void updateFriendActivity(const Friend& frnd);
    void registerContentDialog(ContentDialog& contentDialog) const;

private:
    // QMainWindow overrides
    bool eventFilter(QObject* obj, QEvent* event) final;
    bool event(QEvent* e) final;
    void closeEvent(QCloseEvent* event) final;
    void changeEvent(QEvent* event) final;
    void resizeEvent(QResizeEvent* event) final;
    void moveEvent(QMoveEvent* event) final;

    bool newMessageAlert(QWidget* currentWindow, bool isActive, bool sound = true, bool notify = true);
    void setActiveToolMenuButton(ActiveToolMenuButton newActiveButton);
    void hideMainForms(GenericChatroomWidget* chatroomWidget);
    Group* createGroup(uint32_t groupnumber, const GroupId& groupId);
    void removeFriend(Friend* f, bool fake = false);
    void removeGroup(Group* g, bool fake = false);
    void saveWindowGeometry();
    void saveSplitterGeometry();
    void cycleContacts(bool forward);
    void searchContacts();
    void changeDisplayMode();
    void updateFilterText();
    FilterCriteria getFilterCriteria() const;
    static bool filterGroups(FilterCriteria index);
    static bool filterOnline(FilterCriteria index);
    static bool filterOffline(FilterCriteria index);
    void retranslateUi();
    void focusChatInput();
    void openDialog(GenericChatroomWidget* widget, bool newWindow);
    void playNotificationSound(IAudioSink::Sound sound, bool loop = false);
    void cleanupNotificationSound();
    void acceptFileTransfer(const ToxFile &file, const QString &path);

private:
    Profile& profile;
    std::unique_ptr<QSystemTrayIcon> icon;
    QMenu* trayMenu;
    QAction* statusOnline;
    QAction* statusAway;
    QAction* statusBusy;
    QAction* actionLogout;
    QAction* actionQuit;
    QAction* actionShow;

    QMenu* filterMenu;

    QActionGroup* filterGroup;
    QAction* filterAllAction;
    QAction* filterOnlineAction;
    QAction* filterOfflineAction;
    QAction* filterFriendsAction;
    QAction* filterGroupsAction;

    QActionGroup* filterDisplayGroup;
    QAction* filterDisplayName;
    QAction* filterDisplayActivity;

    Ui::MainWindow* ui;
    QSplitter* centralLayout;
    QPoint dragPosition;
    ContentLayout* contentLayout;
    AddFriendForm* addFriendForm;
    GroupInviteForm* groupInviteForm;

    ProfileInfo* profileInfo;
    ProfileForm* profileForm;

    QPointer<SettingsWidget> settingsWidget;
    std::unique_ptr<UpdateCheck> updateCheck; // ownership should be moved outside Widget once non-singleton
    FilesForm* filesForm;
    static Widget* instance;
    GenericChatroomWidget* activeChatroomWidget;
    FriendListWidget* contactListWidget;
    MaskablePixmapWidget* profilePicture;
    bool notify(QObject* receiver, QEvent* event);
    bool autoAwayActive = false;
    QTimer* timer;
    bool eventFlag;
    bool eventIcon;
    bool wasMaximized = false;
    QPushButton* friendRequestsButton;
    QPushButton* groupInvitesButton;
    unsigned int unreadGroupInvites;
    int icon_size;

    IAudioControl& audio;
    std::unique_ptr<IAudioSink> audioNotification;
    Settings& settings;

    QMap<ToxPk, FriendWidget*> friendWidgets;
    // Shared pointer because qmap copies stuff all over the place
    QMap<ToxPk, std::shared_ptr<FriendMessageDispatcher>> friendMessageDispatchers;
    // Stop gap method of linking our friend messages back to a group id.
    // Eventual goal is to have a notification manager that works on
    // Messages hooked up to message dispatchers but we aren't there
    // yet
    QMap<ToxPk, QMetaObject::Connection> friendAlertConnections;
    QMap<ToxPk, std::shared_ptr<ChatHistory>> friendChatLogs;
    QMap<ToxPk, std::shared_ptr<FriendChatroom>> friendChatrooms;
    QMap<ToxPk, ChatForm*> chatForms;
    std::map<ToxPk, std::unique_ptr<QTimer>> negotiateTimers;

    QMap<GroupId, GroupWidget*> groupWidgets;
    QMap<GroupId, std::shared_ptr<GroupMessageDispatcher>> groupMessageDispatchers;

    // Stop gap method of linking our group messages back to a group id.
    // Eventual goal is to have a notification manager that works on
    // Messages hooked up to message dispatchers but we aren't there
    // yet
    QMap<GroupId, QMetaObject::Connection> groupAlertConnections;
    QMap<GroupId, std::shared_ptr<IChatLog>> groupChatLogs;
    QMap<GroupId, std::shared_ptr<GroupChatroom>> groupChatrooms;
    QMap<GroupId, QSharedPointer<GroupChatForm>> groupChatForms;
    Core* core = nullptr;


    std::unique_ptr<MessageProcessor::SharedParams> sharedMessageProcessorParams;
#if DESKTOP_NOTIFICATIONS
    std::unique_ptr<NotificationGenerator> notificationGenerator;
    DesktopNotify notifier;
#endif

#ifdef Q_OS_MAC
    QAction* fileMenu;
    QAction* editMenu;
    QAction* contactMenu;
    QMenu* changeStatusMenu;
    QAction* editProfileAction;
    QAction* logoutAction;
    QAction* addContactAction;
    QAction* nextConversationAction;
    QAction* previousConversationAction;
#endif
};

bool toxActivateEventHandler(const QByteArray& data);
