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

#ifndef WIDGET_H
#define WIDGET_H

#include "ui_mainwindow.h"

#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>
#include <QSystemTrayIcon>

#include "genericchatitemwidget.h"

#include "src/core/core.h"
#include "src/core/toxfile.h"
#include "src/core/toxid.h"

#define PIXELS_TO_ACT 7

namespace Ui {
class MainWindow;
}

class AddFriendForm;
class Camera;
class ChatForm;
class CircleWidget;
class ContentDialog;
class ContentLayout;
class Core;
class FilesForm;
class Friend;
class FriendListWidget;
class FriendWidget;
class GenericChatroomWidget;
class Group;
class GroupChatForm;
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
    explicit Widget(QWidget* parent = 0);
    ~Widget();
    void init();
    void setCentralWidget(QWidget* widget, const QString& widgetName);
    QString getUsername();
    Camera* getCamera();
    static Widget* getInstance();
    void showUpdateDownloadProgress();
    void addFriendDialog(const Friend* frnd, ContentDialog* dialog);
    void addGroupDialog(Group* group, ContentDialog* dialog);
    bool newFriendMessageAlert(int friendId, bool sound = true);
    bool newGroupMessageAlert(int groupId, bool notify);
    bool getIsWindowMinimized();
    void updateIcons();
    void clearContactsList();

    static QString fromDialogType(DialogType type);
    ContentDialog* createContentDialog() const;
    ContentLayout* createContentDialog(DialogType type) const;

    static void confirmExecutableOpen(const QFileInfo& file);

    void clearAllReceipts();
    void reloadHistory();

    void reloadTheme();
    static QString getStatusIconPath(Status status);
    static inline QIcon prepareIcon(QString path, int w = 0, int h = 0);
    static QPixmap getStatusIconPixmap(QString path, uint32_t w, uint32_t h);
    static QString getStatusTitle(Status status);
    static Status getStatusFromString(QString status);

    void searchCircle(CircleWidget* circleWidget);
    void searchItem(GenericChatItemWidget* chatItem, GenericChatItemWidget::ItemType type);
    bool groupsVisible() const;

    void resetIcon();

public slots:
    void onShowSettings();
    void onSeparateWindowClicked(bool separate);
    void onSeparateWindowChanged(bool separate, bool clicked);
    void setWindowTitle(const QString& title);
    void forceShow();
    void onConnected();
    void onDisconnected();
    void onStatusSet(Status status);
    void onFailedToStartCore();
    void onBadProxyCore();
    void onSelfAvatarLoaded(const QPixmap& pic);
    void setUsername(const QString& username);
    void setStatusMessage(const QString& statusMessage);
    void addFriend(uint32_t friendId, const ToxPk& friendPk);
    void addFriendFailed(const ToxPk& userId, const QString& errorInfo = QString());
    void onFriendStatusChanged(int friendId, Status status);
    void onFriendStatusMessageChanged(int friendId, const QString& message);
    void onFriendDisplayedNameChanged(const QString& displayed);
    void onFriendUsernameChanged(int friendId, const QString& username);
    void onFriendAliasChanged(uint32_t friendId, const QString& alias);
    void onFriendMessageReceived(int friendId, const QString& message, bool isAction);
    void onFriendRequestReceived(const ToxPk& friendPk, const QString& message);
    void updateFriendActivity(const Friend* frnd);
    void onMessageSendResult(uint32_t friendId, const QString& message, int messageId);
    void onReceiptRecieved(int friendId, int receipt);
    void onEmptyGroupCreated(int groupId);
    void onGroupInviteReceived(const GroupInvite& inviteInfo);
    void onGroupInviteAccepted(const GroupInvite& inviteInfo);
    void onGroupMessageReceived(int groupnumber, int peernumber, const QString& message, bool isAction);
    void onGroupNamelistChangedOld(int groupnumber, int peernumber, uint8_t change);
    void onGroupPeerlistChanged(int groupnumber);
    void onGroupPeerNameChanged(int groupnumber, int peernumber, const QString& newName);
    void onGroupTitleChanged(int groupnumber, const QString& author, const QString& title);
    void onGroupPeerAudioPlaying(int groupnumber, int peernumber);
    void onGroupSendFailed(int groupId);
    void onFriendTypingChanged(int friendId, bool isTyping);
    void nextContact();
    void previousContact();
    void onFriendDialogShown(const Friend* f);
    void onGroupDialogShown(Group* g);
    void toggleFullscreen();

signals:
    void friendRequestAccepted(const ToxPk& friendPk);
    void friendRequested(const ToxId& friendAddress, const QString& message);
    void statusSet(Status status);
    void statusSelected(Status status);
    void usernameChanged(const QString& username);
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
    void removeFriend(int friendId);
    void copyFriendIdToClipboard(int friendId);
    void removeGroup(int groupId);
    void setStatusOnline();
    void setStatusAway();
    void setStatusBusy();
    void onIconClick(QSystemTrayIcon::ActivationReason);
    void onUserAwayCheck();
    void onEventIconTick();
    void onTryCreateTrayIcon();
    void onSetShowSystemTray(bool newValue);
    void onSplitterMoved(int pos, int index);
    void processOfflineMsgs();
    void friendListContextMenu(const QPoint& pos);
    void friendRequestsUpdate();
    void groupInvitesUpdate();
    void groupInvitesClear();
    void onDialogShown(GenericChatroomWidget* widget);
    void outgoingNotification();
    void incomingNotification(uint32_t friendId);
    void onRejectCall(uint32_t friendId);
    void onStopNotification();

private:
    // QMainWindow overrides
    bool eventFilter(QObject* obj, QEvent* event) final override;
    bool event(QEvent* e) final override;
    void closeEvent(QCloseEvent* event) final override;
    void changeEvent(QEvent* event) final override;
    void resizeEvent(QResizeEvent* event) final override;
    void moveEvent(QMoveEvent* event) final override;

    bool newMessageAlert(QWidget* currentWindow, bool isActive, bool sound = true, bool notify = true);
    void setActiveToolMenuButton(ActiveToolMenuButton newActiveButton);
    void hideMainForms(GenericChatroomWidget* chatroomWidget);
    Group* createGroup(int groupId);
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

private:
    SystemTrayIcon* icon = nullptr;
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
    FilesForm* filesForm;
    static Widget* instance;
    GenericChatroomWidget* activeChatroomWidget;
    FriendListWidget* contactListWidget;
    MaskablePixmapWidget* profilePicture;
    bool notify(QObject* receiver, QEvent* event);
    bool autoAwayActive = false;
    QTimer *timer, *offlineMsgTimer;
    QRegExp nameMention, sanitizedNameMention;
    bool eventFlag;
    bool eventIcon;
    bool wasMaximized = false;
    QPushButton* friendRequestsButton;
    QPushButton* groupInvitesButton;
    unsigned int unreadGroupInvites;
    int icon_size;

    QMap<uint32_t, GroupWidget*> groupWidgets;
    QMap<uint32_t, FriendWidget*> friendWidgets;
    QMap<uint32_t, ChatForm*> chatForms;
    QMap<uint32_t, GroupChatForm*> groupChatForms;

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

#endif // WIDGET_H
