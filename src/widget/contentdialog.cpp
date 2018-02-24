/*
    Copyright © 2015-2017 by The qTox Project Contributors

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

#include "contentdialog.h"
#include "splitterrestorer.h"

#include <QBoxLayout>
#include <QDragEnterEvent>
#include <QGuiApplication>
#include <QMimeData>
#include <QShortcut>
#include <QSplitter>

#include "contentlayout.h"
#include "friendwidget.h"
#include "groupwidget.h"
#include "style.h"
#include "widget.h"
#include "src/core/core.h"
#include "src/model/friend.h"
#include "src/friendlist.h"
#include "src/model/group.h"
#include "src/grouplist.h"
#include "src/persistence/settings.h"
#include "src/widget/form/chatform.h"
#include "src/widget/friendlistlayout.h"
#include "src/widget/translator.h"
#include "tool/adjustingscrollarea.h"

QString ContentDialog::username = "";
ContentDialog* ContentDialog::currentDialog = nullptr;
QHash<int, std::tuple<ContentDialog*, GenericChatroomWidget*>> ContentDialog::friendList;
QHash<int, std::tuple<ContentDialog*, GenericChatroomWidget*>> ContentDialog::groupList;

static const int minWidget = 220;
static const int minHeight = 220;
static const QSize minSize(minHeight, minWidget);
static const QSize defaultSize(720, 400);

ContentDialog::ContentDialog(QWidget* parent)
    : ActivateDialog(parent, Qt::Window)
    , splitter{new QSplitter(this)}
    , friendLayout{new FriendListLayout(this)}
    , activeChatroomWidget(nullptr)
    , videoSurfaceSize(QSize())
    , videoCount(0)
{
    const Settings& s = Settings::getInstance();
    setStyleSheet(Style::getStylesheet(":/ui/contentDialog/contentDialog.css"));

    friendLayout->setMargin(0);
    friendLayout->setSpacing(0);

    layouts = {friendLayout->getLayoutOnline(), groupLayout.getLayout(),
               friendLayout->getLayoutOffline()};

    if (s.getGroupchatPosition()) {
        layouts.swap(0, 1);
    }

    QWidget* friendWidget = new QWidget();
    friendWidget->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    friendWidget->setAutoFillBackground(true);
    friendWidget->setLayout(friendLayout);

    onGroupchatPositionChanged(s.getGroupchatPosition());

    QScrollArea* friendScroll = new QScrollArea(this);
    friendScroll->setMinimumWidth(minWidget);
    friendScroll->setFrameStyle(QFrame::NoFrame);
    friendScroll->setLayoutDirection(Qt::RightToLeft);
    friendScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    friendScroll->setStyleSheet(Style::getStylesheet(":/ui/friendList/friendList.css"));
    friendScroll->setWidgetResizable(true);
    friendScroll->setWidget(friendWidget);

    QWidget* contentWidget = new QWidget(this);
    contentWidget->setAutoFillBackground(true);

    contentLayout = new ContentLayout(contentWidget);
    contentLayout->setMargin(0);
    contentLayout->setSpacing(0);

    splitter->addWidget(friendScroll);
    splitter->addWidget(contentWidget);
    splitter->setStretchFactor(1, 1);
    splitter->setCollapsible(1, false);

    QVBoxLayout* boxLayout = new QVBoxLayout(this);
    boxLayout->setMargin(0);
    boxLayout->setSpacing(0);
    boxLayout->addWidget(splitter);

    setMinimumSize(minSize);
    setAttribute(Qt::WA_DeleteOnClose);

    QByteArray geometry = s.getDialogGeometry();

    if (!geometry.isNull()) {
        restoreGeometry(geometry);
    } else {
        resize(defaultSize);
    }

    SplitterRestorer restorer(splitter);
    restorer.restore(s.getDialogSplitterState(), size());

    currentDialog = this;
    setAcceptDrops(true);

    new QShortcut(Qt::CTRL + Qt::Key_Q, this, SLOT(close()));
    new QShortcut(Qt::CTRL + Qt::SHIFT + Qt::Key_Tab, this, SLOT(previousContact()));
    new QShortcut(Qt::CTRL + Qt::Key_Tab, this, SLOT(nextContact()));
    new QShortcut(Qt::CTRL + Qt::Key_PageUp, this, SLOT(previousContact()));
    new QShortcut(Qt::CTRL + Qt::Key_PageDown, this, SLOT(nextContact()));

    connect(&s, &Settings::groupchatPositionChanged, this, &ContentDialog::onGroupchatPositionChanged);
    connect(splitter, &QSplitter::splitterMoved, this, &ContentDialog::saveSplitterState);

    Translator::registerHandler(std::bind(&ContentDialog::retranslateUi, this), this);
}

void ContentDialog::removeCurrent(QHash<int, ContactInfo>& infos)
{
    for (auto it = infos.begin(); it != infos.end();) {
        if (std::get<0>(*it) == this) {
            it = infos.erase(it);
        } else {
            ++it;
        }
    }
}

ContentDialog::~ContentDialog()
{
    if (currentDialog == this) {
        currentDialog = nullptr;
    }

    removeCurrent(friendList);
    removeCurrent(groupList);

    Translator::unregister(this);
}

FriendWidget* ContentDialog::addFriend(const Friend* frnd, GenericChatForm* form)
{
    bool compact = Settings::getInstance().getCompactLayout();
    uint32_t friendId = frnd->getId();
    FriendWidget* friendWidget = new FriendWidget(frnd, compact);
    friendLayout->addFriendWidget(friendWidget, frnd->getStatus());
    friendChatForms[friendId] = form;

    connect(friendWidget, &FriendWidget::chatroomWidgetClicked, this, &ContentDialog::activate);
    connect(friendWidget, &FriendWidget::newWindowOpened, this, &ContentDialog::openNewDialog);

    ContentDialog* lastDialog = getFriendDialog(friendId);
    if (lastDialog) {
        lastDialog->removeFriend(friendId);
    }

    friendList.insert(friendId, std::make_tuple(this, friendWidget));
    // FIXME: emit should be removed
    emit friendWidget->chatroomWidgetClicked(friendWidget);

    return friendWidget;
}

GroupWidget* ContentDialog::addGroup(const Group* g, GenericChatForm* form)
{
    const auto groupId = g->getId();
    const auto name = g->getName();
    const auto compact = Settings::getInstance().getCompactLayout();
    GroupWidget* groupWidget = new GroupWidget(groupId, name, compact);
    groupLayout.addSortedWidget(groupWidget);
    groupChatForms[groupId] = form;

    connect(groupWidget, &GroupWidget::chatroomWidgetClicked, this, &ContentDialog::activate);
    connect(groupWidget, &FriendWidget::newWindowOpened, this, &ContentDialog::openNewDialog);

    ContentDialog* lastDialog = getGroupDialog(groupId);

    if (lastDialog) {
        lastDialog->removeGroup(groupId);
    }

    groupList.insert(groupId, std::make_tuple(this, groupWidget));
    // FIXME: emit should be removed
    emit groupWidget->chatroomWidgetClicked(groupWidget);

    return groupWidget;
}

void ContentDialog::removeFriend(int friendId)
{
    auto iter = friendList.find(friendId);
    if (iter == friendList.end()) {
        return;
    }

    FriendWidget* chatroomWidget = static_cast<FriendWidget*>(std::get<1>(iter.value()));

    // Need to find replacement to show here instead.
    if (activeChatroomWidget == chatroomWidget) {
        cycleContacts(true, false);
    }

    friendLayout->removeFriendWidget(chatroomWidget, Status::Offline);
    friendLayout->removeFriendWidget(chatroomWidget, Status::Online);

    chatroomWidget->deleteLater();
    friendList.remove(friendId);

    if (chatroomWidgetCount() == 0) {
        contentLayout->clear();
        activeChatroomWidget = nullptr;
        deleteLater();
    } else {
        update();
    }
}

void ContentDialog::removeGroup(int groupId)
{
    auto iter = groupList.find(groupId);
    if (iter == groupList.end()) {
        return;
    }

    GenericChatroomWidget* chatroomWidget = std::get<1>(iter.value());

    // Need to find replacement to show here instead.
    if (activeChatroomWidget == chatroomWidget) {
        cycleContacts(true, false);
    }

    groupLayout.removeSortedWidget(chatroomWidget);
    chatroomWidget->deleteLater();
    groupList.remove(groupId);

    if (chatroomWidgetCount() == 0) {
        contentLayout->clear();
        activeChatroomWidget = nullptr;
        deleteLater();
    } else {
        update();
    }
}

bool ContentDialog::hasFriendWidget(int friendId, const GenericChatroomWidget* chatroomWidget) const
{
    return hasWidget(friendId, chatroomWidget, friendList);
}

bool ContentDialog::hasGroupWidget(int groupId, const GenericChatroomWidget* chatroomWidget) const
{
    return hasWidget(groupId, chatroomWidget, groupList);
}

int ContentDialog::chatroomWidgetCount() const
{
    return friendLayout->friendTotalCount() + groupLayout.getLayout()->count();
}

void ContentDialog::ensureSplitterVisible()
{
    if (splitter->sizes().at(0) == 0) {
        splitter->setSizes({1, 1});
    }

    update();
}

/**
 * @brief Get current layout and index of current wiget in it.
 * Current layout -- layout contains activated widget.
 *
 * @param[out] layout Current layout
 * @return Index of current widget in current layout.
 */
int ContentDialog::getCurrentLayout(QLayout*& layout)
{
    layout = friendLayout->getLayoutOnline();
    int index = friendLayout->indexOfFriendWidget(activeChatroomWidget, true);
    if (index != -1) {
        return index;
    }

    layout = friendLayout->getLayoutOffline();
    index = friendLayout->indexOfFriendWidget(activeChatroomWidget, false);
    if (index != -1) {
        return index;
    }

    layout = groupLayout.getLayout();
    index = groupLayout.indexOfSortedWidget(activeChatroomWidget);
    if (index != -1) {
        return index;
    }

    layout = nullptr;
    return -1;
}

/**
 * @brief Activate next/previous contact.
 * @param forward If true, activate next contace, previous otherwise.
 * @param inverse ??? TODO: Add docs.
 */
void ContentDialog::cycleContacts(bool forward, bool inverse)
{
    QLayout* currentLayout;
    int index = getCurrentLayout(currentLayout);
    if (!currentLayout || index == -1) {
        return;
    }

    if (!inverse && index == currentLayout->count() - 1) {
        bool groupsOnTop = Settings::getInstance().getGroupchatPosition();
        bool offlineEmpty = friendLayout->getLayoutOffline()->isEmpty();
        bool onlineEmpty = friendLayout->getLayoutOnline()->isEmpty();
        bool groupsEmpty = groupLayout.getLayout()->isEmpty();
        bool isCurOffline = currentLayout == friendLayout->getLayoutOffline();
        bool isCurOnline = currentLayout == friendLayout->getLayoutOnline();
        bool isCurGroup = currentLayout == groupLayout.getLayout();
        bool nextIsEmpty = (isCurOnline && offlineEmpty && (groupsEmpty || groupsOnTop))
                           || (isCurGroup && offlineEmpty && (onlineEmpty || !groupsOnTop))
                           || (isCurOffline);

        if (nextIsEmpty) {
            forward = !forward;
        }
    }

    index += forward ? 1 : -1;
    // If goes out of the layout, then go to the next and skip empty. This loop goes more
    // then 1 time, because for empty layout index will be out of interval (0 < 0 || 0 >= 0)
    while (index < 0 || index >= currentLayout->count()) {
        int oldCount = currentLayout->count();
        currentLayout = nextLayout(currentLayout, forward);
        int newCount = currentLayout->count();
        if (index < 0) {
            index = newCount - 1;
        } else if (index >= oldCount) {
            index = 0;
        }
    }

    QWidget* widget = currentLayout->itemAt(index)->widget();
    GenericChatroomWidget* chatWidget = qobject_cast<GenericChatroomWidget*>(widget);
    if (chatWidget && chatWidget != activeChatroomWidget) {
        // FIXME: emit should be removed
        emit chatWidget->chatroomWidgetClicked(chatWidget);
    }
}

void ContentDialog::onVideoShow(QSize size)
{
    ++videoCount;
    if (videoCount > 1) {
        return;
    }

    videoSurfaceSize = size;
    QSize minSize = minimumSize();
    setMinimumSize(minSize + videoSurfaceSize);
}

void ContentDialog::onVideoHide()
{
    videoCount--;
    if (videoCount > 0) {
        return;
    }

    QSize minSize = minimumSize();
    setMinimumSize(minSize - videoSurfaceSize);
    videoSurfaceSize = QSize();
}

ContentDialog* ContentDialog::current()
{
    return currentDialog;
}

bool ContentDialog::friendWidgetExists(int friendId)
{
    return existsWidget(friendId, friendList);
}

bool ContentDialog::groupWidgetExists(int groupId)
{
    return existsWidget(groupId, groupList);
}

void ContentDialog::focusFriend(int friendId)
{
    focusDialog(friendId, friendList);
}

void ContentDialog::focusGroup(int groupId)
{
    focusDialog(groupId, groupList);
}

void ContentDialog::updateFriendStatus(int friendId)
{
    updateStatus(friendId, friendList);
    ContentDialog* contentDialog = getFriendDialog(friendId);
    if (contentDialog) {
        auto iter = friendList.find(friendId).value();
        GenericChatroomWidget* widget = std::get<1>(iter);
        FriendWidget* friendWidget = static_cast<FriendWidget*>(widget);

        Friend* f = FriendList::findFriend(friendId);
        contentDialog->friendLayout->addFriendWidget(friendWidget, f->getStatus());
    }
}

/**
 * @brief Update friend status message.
 * @param friendId Id friend, whose status was changed.
 * @param message Status message.
 */
void ContentDialog::updateFriendStatusMessage(int friendId, const QString& message)
{
    auto iter = friendList.find(friendId);

    if (iter == friendList.end()) {
        return;
    }

    std::get<1>(iter.value())->setStatusMsg(message);
}

void ContentDialog::updateGroupStatus(int groupId)
{
    updateStatus(groupId, groupList);
}

bool ContentDialog::isFriendWidgetActive(int friendId)
{
    return isWidgetActive(friendId, friendList);
}

bool ContentDialog::isGroupWidgetActive(int groupId)
{
    return isWidgetActive(groupId, groupList);
}

ContentDialog* ContentDialog::getFriendDialog(int friendId)
{
    return getDialog(friendId, friendList);
}

ContentDialog* ContentDialog::getGroupDialog(int groupId)
{
    return getDialog(groupId, groupList);
}

/**
 * @brief Update window title and icon.
 */
void ContentDialog::updateTitleAndStatusIcon()
{
    if (!activeChatroomWidget) {
        setWindowTitle(username);
        return;
    }

    setWindowTitle(activeChatroomWidget->getTitle() + QStringLiteral(" - ") + username);

    bool isGroupchat = activeChatroomWidget->getGroup() != nullptr;
    if (isGroupchat) {
        setWindowIcon(QIcon(":/img/group.svg"));
        return;
    }

    Status currentStatus = activeChatroomWidget->getFriend()->getStatus();

    QMap<Status, QIcon> icons{{Status::Online, QIcon(":/img/status/online.svg")},
                              {Status::Away, QIcon(":/img/status/away.svg")},
                              {Status::Busy, QIcon(":/img/status/busy.svg")},
                              {Status::Offline, QIcon(":/img/status/offline.svg")}};

    setWindowIcon(icons[currentStatus]);
}

/**
 * @brief Update layouts order according to settings.
 * @param groupOnTop If true move groupchat layout on the top. Move under online otherwise.
 */
void ContentDialog::reorderLayouts(bool newGroupOnTop)
{
    bool oldGroupOnTop = layouts.first() == groupLayout.getLayout();
    if (newGroupOnTop != oldGroupOnTop) {
        layouts.swap(0, 1);
    }
}

void ContentDialog::previousContact()
{
    cycleContacts(false);
}

/**
 * @brief Enable next contact.
 */
void ContentDialog::nextContact()
{
    cycleContacts(true);
}

/**
 * @brief Update username to show in the title.
 * @param newName New name to display.
 */
void ContentDialog::setUsername(const QString& newName)
{
    username = newName;
    updateTitleAndStatusIcon();
}

bool ContentDialog::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::WindowActivate:
        if (activeChatroomWidget) {
            activeChatroomWidget->resetEventFlags();
            activeChatroomWidget->updateStatusLight();

            updateTitleAndStatusIcon();

            const Friend* frnd = activeChatroomWidget->getFriend();
            Group* group = activeChatroomWidget->getGroup();

            if (frnd) {
                emit friendDialogShown(frnd);
            } else if (group) {
                emit groupDialogShown(group);
            }
        }

        currentDialog = this;

#ifdef Q_OS_MAC
        emit activated();
#endif
    default:
        break;
    }

    return ActivateDialog::event(event);
}

void ContentDialog::dragEnterEvent(QDragEnterEvent* event)
{
    QObject* o = event->source();
    FriendWidget* frnd = qobject_cast<FriendWidget*>(o);
    GroupWidget* group = qobject_cast<GroupWidget*>(o);
    if (frnd) {
        ToxId toxId(event->mimeData()->text());
        Friend* contact = FriendList::findFriend(toxId.getPublicKey());
        if (!contact) {
            return;
        }

        int friendId = contact->getId();
        auto iter = friendList.find(friendId);

        // If friend is already in a dialog then you can't drop friend where it already is.
        if (iter == friendList.end() || std::get<0>(iter.value()) != this) {
            event->acceptProposedAction();
        }
    } else if (group) {
        if (!event->mimeData()->hasFormat("groupId")) {
            return;
        }

        int groupId = event->mimeData()->data("groupId").toInt();
        Group* contact = GroupList::findGroup(groupId);
        if (!contact) {
            return;
        }

        auto iter = groupList.find(groupId);
        if (iter == groupList.end() || std::get<0>(iter.value()) != this) {
            event->acceptProposedAction();
        }
    }
}

void ContentDialog::dropEvent(QDropEvent* event)
{
    QObject* o = event->source();
    FriendWidget* frnd = qobject_cast<FriendWidget*>(o);
    GroupWidget* group = qobject_cast<GroupWidget*>(o);
    if (frnd) {
        ToxId toxId(event->mimeData()->text());
        Friend* contact = FriendList::findFriend(toxId.getPublicKey());
        if (!contact) {
            return;
        }

        int friendId = contact->getId();
        auto iter = friendList.find(friendId);
        if (iter != friendList.end()) {
            std::get<0>(iter.value())->removeFriend(friendId);
        }

        Widget::getInstance()->addFriendDialog(contact, this);
        ensureSplitterVisible();
    } else if (group) {
        if (!event->mimeData()->hasFormat("groupId")) {
            return;
        }

        int groupId = event->mimeData()->data("groupId").toInt();
        Group* contact = GroupList::findGroup(groupId);
        if (!contact) {
            return;
        }

        auto iter = friendList.find(groupId);
        if (iter != friendList.end()) {
            std::get<0>(iter.value())->removeGroup(groupId);
        }

        Widget::getInstance()->addGroupDialog(contact, this);
        ensureSplitterVisible();
    }
}

void ContentDialog::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow()) {
            currentDialog = this;
        }
    }
}

void ContentDialog::resizeEvent(QResizeEvent* event)
{
    saveDialogGeometry();
    QDialog::resizeEvent(event);
}

void ContentDialog::moveEvent(QMoveEvent* event)
{
    saveDialogGeometry();
    QDialog::moveEvent(event);
}

void ContentDialog::keyPressEvent(QKeyEvent* event)
{
    // Ignore escape keyboard shortcut.
    if (event->key() != Qt::Key_Escape) {
        QDialog::keyPressEvent(event);
    }
}

/**
 * @brief Open a new dialog window associated with widget
 * @param widget Widget associated with contact.
 */
void ContentDialog::openNewDialog(GenericChatroomWidget* widget)
{
    ContentDialog* contentDialog = new ContentDialog();
    contentDialog->show();

    if (widget->getFriend()) {
        removeFriend(widget->getFriend()->getId());
        Widget::getInstance()->addFriendDialog(widget->getFriend(), contentDialog);
    } else {
        removeGroup(widget->getGroup()->getId());
        Widget::getInstance()->addGroupDialog(widget->getGroup(), contentDialog);
    }

    contentDialog->raise();
    contentDialog->activateWindow();
}

/**
 * @brief Show ContentDialog, activate chatroom widget.
 * @param widget Widget which should be activated.
 */
void ContentDialog::activate(GenericChatroomWidget* widget)
{
    // If we clicked on the currently active widget, don't reload and relayout everything
    if (activeChatroomWidget == widget) {
        return;
    }

    contentLayout->clear();

    if (activeChatroomWidget) {
        activeChatroomWidget->setAsInactiveChatroom();
    }

    activeChatroomWidget = widget;

    const FriendWidget* const friendWidget = qobject_cast<FriendWidget*>(widget);
    if (friendWidget) {
        auto friendId = friendWidget->getFriend()->getId();
        friendChatForms[friendId]->show(contentLayout);
    } else {
        auto groupId = widget->getGroup()->getId();
        groupChatForms[groupId]->show(contentLayout);
    }

    widget->setAsActiveChatroom();
    widget->resetEventFlags();
    widget->updateStatusLight();
    updateTitleAndStatusIcon();
}

/**
 * @brief Handler of `groupchatPositionChanged` action.
 * Move group layout on the top or on the buttom.
 *
 * @param top If true, move group layout on the top, false otherwise.
 */
void ContentDialog::onGroupchatPositionChanged(bool top)
{
    friendLayout->removeItem(groupLayout.getLayout());
    friendLayout->insertLayout(top ? 0 : 1, groupLayout.getLayout());
}

/**
 * @brief Retranslate all elements in the form.
 */
void ContentDialog::retranslateUi()
{
    updateTitleAndStatusIcon();
}

/**
 * @brief Save size of dialog window.
 */
void ContentDialog::saveDialogGeometry()
{
    Settings::getInstance().setDialogGeometry(saveGeometry());
}

/**
 * @brief Save state of splitter between dialog and dialog list.
 */
void ContentDialog::saveSplitterState()
{
    Settings::getInstance().setDialogSplitterState(splitter->saveState());
}

/**
 * @brief Check if current ContentDialog instance and chatroom widget associated with user.
 * @param id User Id.
 * @param chatroomWidget Widget which should be a pair for current dialog.
 * @param list List with contact info.
 * @return True, if chatroomWidget is pair for current instance.
 */
bool ContentDialog::hasWidget(int id, const GenericChatroomWidget* chatroomWidget,
                              const QHash<int, ContactInfo>& list) const
{
    auto iter = list.find(id);
    if (iter == list.end()) {
        return false;
    }

    return std::get<0>(*iter) == this && std::get<1>(*iter) == chatroomWidget;
}

/**
 * @brief Focus the dialog if it exists.
 * @param id User Id.
 * @param list List with contact info.
 */
void ContentDialog::focusDialog(int id, const QHash<int, ContactInfo>& list)
{
    auto iter = list.find(id);
    if (iter == list.end()) {
        return;
    }

    ContentDialog* dialog = std::get<0>(*iter);
    if (dialog->windowState() & Qt::WindowMinimized) {
        dialog->showNormal();
    }

    dialog->raise();
    dialog->activateWindow();
    dialog->activate(std::get<1>(iter.value()));
}

/**
 * @brief Check, if widget is exists.
 * @param id User Id.
 * @param list List with contact info.
 * @return True is widget exists, false otherwise.
 */
bool ContentDialog::existsWidget(int id, const QHash<int, ContactInfo>& list)
{
    auto iter = list.find(id);
    return iter != list.end();
}

/**
 * @brief Update widget status and dialog title for current user.
 * @param id User Id.
 * @param list List with contact info.
 */
void ContentDialog::updateStatus(int id, const QHash<int, ContactInfo>& list)
{
    auto iter = list.find(id);
    if (iter == list.end()) {
        return;
    }

    GenericChatroomWidget* chatroomWidget = std::get<1>(*iter);
    chatroomWidget->updateStatusLight();

    if (chatroomWidget->isActive()) {
        ContentDialog* dialog = std::get<0>(*iter);
        dialog->updateTitleAndStatusIcon();
    }
}

/**
 * @brief Check, if user dialog is active.
 * @param id User Id.
 * @param list List with contact info.
 * @return True if user dialog is active, false otherwise.
 */
bool ContentDialog::isWidgetActive(int id, const QHash<int, ContactInfo>& list)
{
    auto iter = list.find(id);
    if (iter == list.end()) {
        return false;
    }

    return std::get<0>(iter.value())->activeChatroomWidget == std::get<1>(iter.value());
}

/**
 * @brief Select ContentDialog by id from the list.
 * @param id User Id.
 * @param list List with contact info.
 * @return ContentDialog for user and nullptr if not found.
 */
ContentDialog* ContentDialog::getDialog(int id, const QHash<int, ContactInfo>& list)
{
    auto iter = list.find(id);
    if (iter == list.end()) {
        return nullptr;
    }

    return std::get<0>(iter.value());
}

/**
 * @brief Find the next or previous layout in layout list.
 * @param layout Current layout.
 * @param forward If true, move forward, backward othwerwise.
 * @return Next/previous layout.
 */
QLayout* ContentDialog::nextLayout(QLayout* layout, bool forward) const
{
    int index = layouts.indexOf(layout);
    if (index == -1) {
        return nullptr;
    }

    int next = forward ? index + 1 : index - 1;
    size_t size = layouts.size();
    next = (next + size) % size;

    return layouts[next];
}
