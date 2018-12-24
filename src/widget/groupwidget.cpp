/*
    Copyright © 2014-2018 by The qTox Project Contributors

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

#include "groupwidget.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QMenu>
#include <QMimeData>
#include <QPalette>

#include "contentdialog.h"
#include "maskablepixmapwidget.h"
#include "form/groupchatform.h"
#include "src/core/core.h"
#include "src/model/friend.h"
#include "src/friendlist.h"
#include "src/model/group.h"
#include "src/grouplist.h"
#include "src/widget/friendwidget.h"
#include "src/widget/style.h"
#include "src/widget/translator.h"
#include "tool/croppinglabel.h"

GroupWidget::GroupWidget(std::shared_ptr<GroupChatroom> chatroom, bool compact)
    : GenericChatroomWidget(compact)
    , groupId{static_cast<int>(chatroom->getGroup()->getId())}
    , chatroom{chatroom}
{
    avatar->setPixmap(Style::scaleSvgImage(":img/group.svg", avatar->width(), avatar->height()));
    statusPic.setPixmap(QPixmap(":img/status/online.svg"));
    statusPic.setMargin(3);

    Group* g = chatroom->getGroup();
    nameLabel->setText(g->getName());

    updateUserCount();
    setAcceptDrops(true);

    connect(g, &Group::titleChanged, this, &GroupWidget::updateTitle);
    connect(g, &Group::userListChanged, this, &GroupWidget::updateUserCount);
    connect(nameLabel, &CroppingLabel::editFinished, g, &Group::setName);
    Translator::registerHandler(std::bind(&GroupWidget::retranslateUi, this), this);
}

GroupWidget::~GroupWidget()
{
    Translator::unregister(this);
}

void GroupWidget::updateTitle(uint32_t groupId, const QString& author, const QString& newName)
{
    Q_UNUSED(groupId);
    Q_UNUSED(author);
    nameLabel->setText(newName);
}

void GroupWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (!active) {
        setBackgroundRole(QPalette::Highlight);
    }

    installEventFilter(this); // Disable leave event.

    QMenu menu(this);

    QAction* openChatWindow = nullptr;
    QAction* removeChatWindow = nullptr;

    // TODO: Move to model
    ContentDialog* contentDialog = ContentDialog::getGroupDialog(groupId);
    const bool notAlone = contentDialog != nullptr && contentDialog->chatroomWidgetCount() > 1;

    if (contentDialog == nullptr || notAlone) {
        openChatWindow = menu.addAction(tr("Open chat in new window"));
    }

    if (contentDialog && contentDialog->hasGroupWidget(groupId, this)) {
        removeChatWindow = menu.addAction(tr("Remove chat from this window"));
    }

    menu.addSeparator();

    QAction* setTitle = menu.addAction(tr("Set title..."));
    QAction* quitGroup = menu.addAction(tr("Quit group", "Menu to quit a groupchat"));

    QAction* selectedItem = menu.exec(event->globalPos());

    removeEventFilter(this);

    if (!active) {
        setBackgroundRole(QPalette::Window);
    }

    if (!selectedItem) {
        return;
    }

    if (selectedItem == quitGroup) {
        emit removeGroup(groupId);
    } else if (selectedItem == openChatWindow) {
        emit newWindowOpened(this);
    } else if (selectedItem == removeChatWindow) {
        // TODO: move to model
        ContentDialog* contentDialog = ContentDialog::getGroupDialog(groupId);
        contentDialog->removeGroup(groupId);
    } else if (selectedItem == setTitle) {
        editName();
    }
}

void GroupWidget::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) {
        dragStartPos = ev->pos();
    }

    GenericChatroomWidget::mousePressEvent(ev);
}

void GroupWidget::mouseMoveEvent(QMouseEvent* ev)
{
    if (!(ev->buttons() & Qt::LeftButton)) {
        return;
    }

    if ((dragStartPos - ev->pos()).manhattanLength() > QApplication::startDragDistance()) {
        QMimeData* mdata = new QMimeData;
        mdata->setText(getGroup()->getName());
        mdata->setData("groupId", QByteArray::number(getGroup()->getId()));

        QDrag* drag = new QDrag(this);
        drag->setMimeData(mdata);
        drag->setPixmap(avatar->getPixmap());
        drag->exec(Qt::CopyAction | Qt::MoveAction);
    }
}

void GroupWidget::updateUserCount()
{
    int peersCount = chatroom->getGroup()->getPeersCount();
    statusMessageLabel->setText(tr("%n user(s) in chat", "", peersCount));
}

void GroupWidget::setAsActiveChatroom()
{
    setActive(true);
    avatar->setPixmap(Style::scaleSvgImage(":img/group_dark.svg", avatar->width(), avatar->height()));
}

void GroupWidget::setAsInactiveChatroom()
{
    setActive(false);
    avatar->setPixmap(Style::scaleSvgImage(":img/group.svg", avatar->width(), avatar->height()));
}

void GroupWidget::updateStatusLight()
{
    Group* g = chatroom->getGroup();

    if (g->getEventFlag()) {
        statusPic.setPixmap(QPixmap(":img/status/online_notification.svg"));
        statusPic.setMargin(1);
    } else {
        statusPic.setPixmap(QPixmap(":img/status/online.svg"));
        statusPic.setMargin(3);
    }
}

QString GroupWidget::getStatusString() const
{
    if (chatroom->hasNewMessage()) {
        return tr("New Message");
    } else {
        return tr("Online");
    }
}

void GroupWidget::editName()
{
    nameLabel->editBegin();
}

// TODO: Remove
Group* GroupWidget::getGroup() const
{
    return chatroom->getGroup();
}

void GroupWidget::resetEventFlags()
{
    chatroom->resetEventFlags();
}

void GroupWidget::dragEnterEvent(QDragEnterEvent* ev)
{
    // TODO: Send ToxPk in mimeData
    const ToxId toxId = ToxId(ev->mimeData()->text());
    const ToxPk pk = toxId.getPublicKey();
    if (chatroom->friendExists(pk)) {
        ev->acceptProposedAction();
    }

    if (!active) {
        setBackgroundRole(QPalette::Highlight);
    }
}

void GroupWidget::dragLeaveEvent(QDragLeaveEvent*)
{
    if (!active) {
        setBackgroundRole(QPalette::Window);
    }
}

void GroupWidget::dropEvent(QDropEvent* ev)
{
    const ToxId toxId = ToxId(ev->mimeData()->text());
    const ToxPk pk = toxId.getPublicKey();
    if (!chatroom->friendExists(pk)) {
        return;
    }

    chatroom->inviteFriend(pk);

    if (!active) {
        setBackgroundRole(QPalette::Window);
    }
}

void GroupWidget::setName(const QString& name)
{
    nameLabel->setText(name);
}

void GroupWidget::retranslateUi()
{
    updateUserCount();
}
