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

#include "friendwidget.h"

#include "circlewidget.h"
#include "contentdialog.h"
#include "friendlistwidget.h"
#include "groupwidget.h"
#include "maskablepixmapwidget.h"

#include "src/core/core.h"
#include "src/model/friend.h"
#include "src/model/about/aboutfriend.h"
#include "src/friendlist.h"
#include "src/model/group.h"
#include "src/grouplist.h"
#include "src/persistence/settings.h"
#include "src/widget/about/aboutfriendform.h"
#include "src/widget/form/chatform.h"
#include "src/widget/style.h"
#include "src/widget/tool/croppinglabel.h"
#include "src/widget/widget.h"

#include <QApplication>
#include <QBitmap>
#include <QCollator>
#include <QContextMenuEvent>
#include <QDebug>
#include <QDrag>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMimeData>

#include <cassert>

namespace
{
constexpr auto MAX_NAME_LENGTH = 30;
}

/**
 * @class FriendWidget
 *
 * Widget, which displays brief information about friend.
 * For example, used on friend list.
 * When you click should open the chat with friend. Widget has a context menu.
 */

FriendWidget::FriendWidget(const Friend* f, bool compact)
    : GenericChatroomWidget(compact)
    , frnd{f}
    , isDefaultAvatar{true}
{
    avatar->setPixmap(QPixmap(":/img/contact.svg"));
    statusPic.setPixmap(QPixmap(":/img/status/offline.svg"));
    statusPic.setMargin(3);
    setName(f->getDisplayedName());
    nameLabel->setTextFormat(Qt::PlainText);
    // update on changes of the displayed name
    connect(f, &Friend::displayedNameChanged,
            [this](const QString& displayed) {this->setName(displayed);});
    // update alias when edited
    connect(nameLabel, &CroppingLabel::editFinished, f, &Friend::setAlias);
    statusMessageLabel->setTextFormat(Qt::PlainText);
}

/**
 * @brief FriendWidget::contextMenuEvent
 * @param event Describe a context menu event
 *
 * Default context menu event handler.
 * Redirect all event information to the signal.
 */
void FriendWidget::contextMenuEvent(QContextMenuEvent* event)
{
    emit contextMenuCalled(event);
}

/**
 * @brief FriendWidget::onContextMenuCalled
 * @param event Redirected from native contextMenuEvent
 *
 * Context menu handler. Always should be called to FriendWidget from FriendList
 */
void FriendWidget::onContextMenuCalled(QContextMenuEvent* event)
{
    if (!active) {
        setBackgroundRole(QPalette::Highlight);
    }

    installEventFilter(this); // Disable leave event.

    QMenu menu;

    const auto friendId = frnd->getId();
    const ContentDialog* contentDialog = ContentDialog::getFriendDialog(friendId);

    if (!contentDialog || contentDialog->chatroomWidgetCount() > 1) {
        const auto openChatWindow = menu.addAction(tr("Open chat in new window"));
        connect(openChatWindow, &QAction::triggered, [=]() { emit newWindowOpened(this); });
    }

    if (contentDialog && contentDialog->hasFriendWidget(friendId, this)) {
        const auto removeChatWindow = menu.addAction(tr("Remove chat from this window"));
        connect(removeChatWindow, &QAction::triggered, this, &FriendWidget::removeChatWindow);
    }

    menu.addSeparator();
    QMenu* inviteMenu = menu.addMenu(tr("Invite to group",
                                        "Menu to invite a friend to a groupchat"));
    inviteMenu->setEnabled(frnd->getStatus() != Status::Offline);
    const auto newGroupAction = inviteMenu->addAction(tr("To new group"));
    connect(newGroupAction, &QAction::triggered, this, &FriendWidget::moveToNewGroup);
    inviteMenu->addSeparator();

    for (const Group* group : GroupList::getAllGroups()) {
        auto name = group->getName();
        if (name.length() > MAX_NAME_LENGTH) {
            name = name.left(MAX_NAME_LENGTH).trimmed() + "..";
        }
        const auto groupAction = inviteMenu->addAction(tr("Invite to group '%1'").arg(name));
        connect(groupAction, &QAction::triggered, [=]() { inviteFriend(friendId, group); });
    }

    const auto& s = Settings::getInstance();
    const auto circleId = s.getFriendCircleID(frnd->getPublicKey());
    auto circleWidget = CircleWidget::getFromID(circleId);
    auto w = circleWidget ? circleWidget : static_cast<QWidget*>(this);
    auto friendList = qobject_cast<FriendListWidget*>(w->parentWidget());

    auto circleMenu = menu.addMenu(tr("Move to circle...",
                                        "Menu to move a friend into a different circle"));

    const auto pk = frnd->getPublicKey();
    const auto newCircleAction = circleMenu->addAction(tr("To new circle"));
    connect(newCircleAction, &QAction::triggered, this, &FriendWidget::moveToNewCircle);

    if (circleId != -1) {
        const QString circleName = s.getCircleName(circleId);
        const auto removeCircleAction = circleMenu->addAction(
                    tr("Remove from circle '%1'").arg(circleName));
        connect(removeCircleAction, &QAction::triggered, this, &FriendWidget::removeFromCircle);
    }

    circleMenu->addSeparator();

    QList<QAction*> circleActionList;
    for (int i = 0; i < s.getCircleCount(); ++i) {
        if (i == circleId) {
            continue;
        }

        const auto name = s.getCircleName(i);
        QAction* action = new QAction(tr("Move  to circle \"%1\"").arg(name), circleMenu);
        connect(action, &QAction::triggered, [=]() { moveToCircle(i); });
        circleActionList.push_back(action);
    }

    std::sort(circleActionList.begin(), circleActionList.end(),
              [](const QAction* lhs, const QAction* rhs) -> bool {
                  QCollator collator;
                  collator.setNumericMode(true);
                  return collator.compare(lhs->text(), rhs->text()) < 0;
              });

    circleMenu->addActions(circleActionList);

    const auto setAlias = menu.addAction(tr("Set alias..."));
    connect(setAlias, &QAction::triggered, [this]() { nameLabel->editBegin(); });

    menu.addSeparator();
    auto autoAccept = menu.addAction(tr("Auto accept files from this friend",
                                            "context menu entry"));
    const auto dir = s.getAutoAcceptDir(pk);
    autoAccept->setCheckable(true);
    autoAccept->setChecked(!dir.isEmpty());
    connect(autoAccept, &QAction::triggered, this, &FriendWidget::changeAutoAccept);
    menu.addSeparator();

    if (!contentDialog || !contentDialog->hasFriendWidget(friendId, this)) {
        const auto removeAction = menu.addAction(
                    tr("Remove friend", "Menu to remove the friend from our friendlist"));
        connect(removeAction, &QAction::triggered, [=]() { emit removeFriend(friendId); });
    }

    menu.addSeparator();
    const auto aboutWindow = menu.addAction(tr("Show details"));
    connect(aboutWindow, &QAction::triggered, this, &FriendWidget::showDetails);

    const auto pos = event->globalPos();
    menu.exec(pos);

    removeEventFilter(this);

    if (!active) {
        setBackgroundRole(QPalette::Window);
    }
}

void FriendWidget::removeChatWindow()
{
    const auto friendId = frnd->getId();
    ContentDialog* contentDialog = ContentDialog::getFriendDialog(friendId);
    contentDialog->removeFriend(friendId);
}

void FriendWidget::moveToNewGroup()
{
    const auto friendId = frnd->getId();
    const auto groupId = Core::getInstance()->createGroup();
    Core::getInstance()->groupInviteFriend(friendId, groupId);
}

void FriendWidget::inviteFriend(uint32_t friendId, const Group* group)
{
    Core::getInstance()->groupInviteFriend(friendId, group->getId());
}

namespace
{

std::tuple<CircleWidget*, FriendListWidget*> getCircleAndFriendList(
        const Friend* frnd, FriendWidget* fw)
{
    const auto pk = frnd->getPublicKey();
    const auto circleId = Settings::getInstance().getFriendCircleID(pk);
    auto circleWidget = CircleWidget::getFromID(circleId);
    auto w = circleWidget ? static_cast<QWidget*>(circleWidget) : static_cast<QWidget*>(fw);
    auto friendList = qobject_cast<FriendListWidget*>(w->parentWidget());
    return std::make_tuple(circleWidget, friendList);
}

}

void FriendWidget::moveToNewCircle()
{
    CircleWidget* circleWidget;
    FriendListWidget* friendList;
    std::tie(circleWidget, friendList) = getCircleAndFriendList(frnd, this);

    if (circleWidget != nullptr) {
        circleWidget->updateStatus();
    }

    if (friendList != nullptr) {
        friendList->addCircleWidget(this);
    } else {
        const auto pk = frnd->getPublicKey();
        auto &s = Settings::getInstance();
        auto circleId = s.addCircle();
        s.setFriendCircleID(pk, circleId);
    }
}

void FriendWidget::removeFromCircle()
{
    CircleWidget* circleWidget;
    FriendListWidget* friendList;
    std::tie(circleWidget, friendList) = getCircleAndFriendList(frnd, this);

    if (friendList != nullptr) {
        friendList->moveWidget(this, frnd->getStatus(), true);
    } else {
        const auto pk = frnd->getPublicKey();
        auto& s = Settings::getInstance();
        s.setFriendCircleID(pk, -1);
    }

    if (circleWidget != nullptr) {
        circleWidget->updateStatus();
        Widget::getInstance()->searchCircle(circleWidget);
    }
}

void FriendWidget::moveToCircle(int newCircleId)
{
    const auto pk = frnd->getPublicKey();
    const auto oldCircleId = Settings::getInstance().getFriendCircleID(pk);
    auto& s = Settings::getInstance();
    auto oldCircleWidget = CircleWidget::getFromID(oldCircleId);
    auto newCircleWidget = CircleWidget::getFromID(newCircleId);

    if (newCircleWidget) {
        newCircleWidget->addFriendWidget(this, frnd->getStatus());
        newCircleWidget->setExpanded(true);
        Widget::getInstance()->searchCircle(newCircleWidget);
        s.savePersonal();
    } else {
        s.setFriendCircleID(pk, newCircleId);
    }

    if (oldCircleWidget) {
        oldCircleWidget->updateStatus();
        Widget::getInstance()->searchCircle(oldCircleWidget);
    }
}

void FriendWidget::changeAutoAccept(bool enable)
{
    const auto pk = frnd->getPublicKey();
    auto &s = Settings::getInstance();
    if (enable) {
        const auto oldDir = s.getAutoAcceptDir(pk);
        const auto newDir = QFileDialog::getExistingDirectory(
            Q_NULLPTR, tr("Choose an auto accept directory", "popup title"), oldDir);

        const auto friendId = frnd->getId();
        qDebug() << "Setting auto accept dir for" << friendId << "to" << newDir;
        s.setAutoAcceptDir(pk, newDir);
    } else {
        qDebug() << "not checked";
        s.setAutoAcceptDir(pk, "");
    }
}
void FriendWidget::showDetails()
{
    const QPointer<IAboutFriend> about = new AboutFriend(frnd, &Settings::getInstance());
    auto aboutUser = new AboutFriendForm(about, Widget::getInstance());
    aboutUser->show();
}

void FriendWidget::setAsActiveChatroom()
{
    setActive(true);

    if (isDefaultAvatar) {
        avatar->setPixmap(QPixmap(":img/contact_dark.svg"));
    }
}

void FriendWidget::setAsInactiveChatroom()
{
    setActive(false);

    if (isDefaultAvatar) {
        avatar->setPixmap(QPixmap(":img/contact.svg"));
    }
}

void FriendWidget::updateStatusLight()
{
    static const QString statuses[] = {
        ":img/status/online.svg",
        ":img/status/online_notification.svg",
        ":img/status/away.svg",
        ":img/status/away_notification.svg",
        ":img/status/busy.svg",
        ":img/status/busy_notification.svg",
        ":img/status/offline.svg",
        ":img/status/offline_notification.svg",
    };

    const bool event = frnd->getEventFlag();
    const int index = static_cast<int>(frnd->getStatus()) * 2 + event;
    statusPic.setPixmap(QPixmap(statuses[index]));

    if (event) {
        const Settings& s = Settings::getInstance();
        const uint32_t circleId = s.getFriendCircleID(frnd->getPublicKey());
        CircleWidget* circleWidget = CircleWidget::getFromID(circleId);
        if (circleWidget) {
            circleWidget->setExpanded(true);
        }

        Widget::getInstance()->updateFriendActivity(frnd);
    }

    statusPic.setMargin(event ? 1 : 3);
}

QString FriendWidget::getStatusString() const
{
    const int status = static_cast<int>(frnd->getStatus());
    const bool event = frnd->getEventFlag();

    static const QVector<QString> names = {
        tr("Online"),
        tr("Away"),
        tr("Busy"),
        tr("Offline"),
    };

    return event ? tr("New message") : names.value(status);
}

const Friend* FriendWidget::getFriend() const
{
    return frnd;
}

void FriendWidget::search(const QString& searchString, bool hide)
{
    searchName(searchString, hide);
    const Settings& s = Settings::getInstance();
    const uint32_t circleId = s.getFriendCircleID(frnd->getPublicKey());
    CircleWidget* circleWidget = CircleWidget::getFromID(circleId);
    if (circleWidget) {
        circleWidget->search(searchString);
    }
}

void FriendWidget::resetEventFlags()
{
    // Hack to avoid edit const Friend. TODO: Repalce on emit
    Friend* f = FriendList::findFriend(frnd->getId());
    f->setEventFlag(false);
}

void FriendWidget::onAvatarChange(uint32_t friendId, const QPixmap& pic)
{
    if (friendId != frnd->getId()) {
        return;
    }

    isDefaultAvatar = false;
    avatar->setPixmap(pic);
}

void FriendWidget::onAvatarRemoved(uint32_t friendId)
{
    if (friendId != frnd->getId()) {
        return;
    }

    isDefaultAvatar = true;

    const QString path = QString(":/img/contact%1.svg").arg(isActive() ? "_dark" : "");
    avatar->setPixmap(QPixmap(path));
}

void FriendWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) {
        dragStartPos = ev->pos();
    }

    GenericChatroomWidget::mousePressEvent(ev);
}

void FriendWidget::mouseMoveEvent(QMouseEvent* ev)
{
    if (!(ev->buttons() & Qt::LeftButton)) {
        return;
    }

    const int distance = (dragStartPos - ev->pos()).manhattanLength();
    if (distance > QApplication::startDragDistance()) {
        QMimeData* mdata = new QMimeData;
        mdata->setText(getFriend()->getPublicKey().toString());

        QDrag* drag = new QDrag(this);
        drag->setMimeData(mdata);
        drag->setPixmap(avatar->getPixmap());
        drag->exec(Qt::CopyAction | Qt::MoveAction);
    }
}
