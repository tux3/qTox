/*
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

#ifndef FRIENDWIDGET_H
#define FRIENDWIDGET_H

#include "genericchatroomwidget.h"

class QPixmap;
class MaskablePixmapWidget;

class FriendWidget : public GenericChatroomWidget
{
    Q_OBJECT
public:
    FriendWidget(const Friend* f, bool compact);
    void contextMenuEvent(QContextMenuEvent* event) override final;
    void setAsActiveChatroom() override final;
    void setAsInactiveChatroom() override final;
    void updateStatusLight() override final;
    void resetEventFlags() override final;
    QString getStatusString() const override final;
    const Friend* getFriend() const override final;

    void search(const QString& searchString, bool hide = false);

signals:
    void friendWidgetClicked(FriendWidget* widget);
    void removeFriend(int friendId);
    void copyFriendIdToClipboard(int friendId);
    void contextMenuCalled(QContextMenuEvent* event);

public slots:
    void onAvatarChange(uint32_t friendId, const QPixmap& pic);
    void onAvatarRemoved(uint32_t friendId);
    void onContextMenuCalled(QContextMenuEvent* event);

protected:
    virtual void mousePressEvent(QMouseEvent* ev) override;
    virtual void mouseMoveEvent(QMouseEvent* ev) override;
    void setFriendAlias();

private slots:
    void removeChatWindow();
    void moveToNewGroup();
    void inviteFriend(uint32_t friendId, const Group* group);
    void moveToNewCircle();
    void removeFromCircle();
    void moveToCircle(int circleId);
    void changeAutoAccept(bool enable);
    void showDetails();

public:
    const Friend* frnd;
    bool isDefaultAvatar;
};

#endif // FRIENDWIDGET_H
