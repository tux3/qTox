/*
    Copyright © 2015 by The qTox Project Contributors

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

#ifndef CONTENTDIALOG_H
#define CONTENTDIALOG_H

#include <tuple>

#include "src/widget/genericchatitemlayout.h"
#include "src/widget/tool/activatedialog.h"

template <typename K, typename V>
class QHash;
template <typename T>
class QSet;

class QSplitter;
class QVBoxLayout;
class ContentDialog;
class ContentLayout;
class GenericChatForm;
class GenericChatroomWidget;
class FriendWidget;
class GroupWidget;
class FriendListLayout;
class Friend;
class Group;

using ContactInfo = std::tuple<ContentDialog*, GenericChatroomWidget*>;

class ContentDialog : public ActivateDialog
{
    Q_OBJECT
public:
    explicit ContentDialog(QWidget* parent = nullptr);
    ~ContentDialog() override;

    FriendWidget* addFriend(const Friend* f, GenericChatForm* form);
    GroupWidget* addGroup(const Group* g, GenericChatForm* form);
    void removeFriend(int friendId);
    void removeGroup(int groupId);
    bool hasFriendWidget(int friendId, const GenericChatroomWidget* chatroomWidget) const;
    bool hasGroupWidget(int groupId, const GenericChatroomWidget* chatroomWidget) const;
    int chatroomWidgetCount() const;
    void ensureSplitterVisible();
    void updateTitleAndStatusIcon();

    void cycleContacts(bool forward, bool loop = true);
    void onVideoShow(QSize size);
    void onVideoHide();

    static ContentDialog* current();
    static bool friendWidgetExists(int friendId);
    static bool groupWidgetExists(int groupId);
    static void focusFriend(int friendId);
    static void focusGroup(int groupId);
    static void updateFriendStatus(int friendId);
    static void updateFriendStatusMessage(int friendId, const QString& message);
    static void updateGroupStatus(int groupId);
    static bool isFriendWidgetActive(int friendId);
    static bool isGroupWidgetActive(int groupId);
    static ContentDialog* getFriendDialog(int friendId);
    static ContentDialog* getGroupDialog(int groupId);

signals:
    void friendDialogShown(const Friend* f);
    void groupDialogShown(Group* g);
    void activated();

public slots:
    void reorderLayouts(bool newGroupOnTop);
    void previousContact();
    void nextContact();
    void setUsername(const QString& newName);

protected:
    bool event(QEvent* event) final override;
    void dragEnterEvent(QDragEnterEvent* event) final override;
    void dropEvent(QDropEvent* event) final override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void activate(GenericChatroomWidget* widget);
    void openNewDialog(GenericChatroomWidget* widget);
    void onGroupchatPositionChanged(bool top);

private:
    void retranslateUi();
    void saveDialogGeometry();
    void saveSplitterState();
    QLayout* nextLayout(QLayout* layout, bool forward) const;
    int getCurrentLayout(QLayout*& layout);

    bool hasWidget(int id, const GenericChatroomWidget* chatroomWidget,
                   const QHash<int, ContactInfo>& list) const;
    void removeCurrent(QHash<int, ContactInfo>& infos);
    static bool existsWidget(int id, const QHash<int, ContactInfo>& list);
    static void focusDialog(int id, const QHash<int, ContactInfo>& list);
    static void updateStatus(int id, const QHash<int, ContactInfo>& list);
    static bool isWidgetActive(int id, const QHash<int, ContactInfo>& list);
    static ContentDialog* getDialog(int id, const QHash<int, ContactInfo>& list);

    QList<QLayout*> layouts;
    QSplitter* splitter;
    FriendListLayout* friendLayout;
    GenericChatItemLayout groupLayout;
    ContentLayout* contentLayout;
    GenericChatroomWidget* activeChatroomWidget;
    QSize videoSurfaceSize;
    int videoCount;

    static QString username;
    static ContentDialog* currentDialog;
    static QHash<int, ContactInfo> friendList;
    static QHash<int, ContactInfo> groupList;
    QHash<int, GenericChatForm*> friendChatForms;
    QHash<int, GenericChatForm*> groupChatForms;
};

#endif // CONTENTDIALOG_H
