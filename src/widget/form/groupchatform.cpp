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

#include "groupchatform.h"

#include "tabcompleter.h"
#include "src/core/core.h"
#include "src/core/coreav.h"
#include "src/core/groupid.h"
#include "src/chatlog/chatlog.h"
#include "src/chatlog/content/text.h"
#include "src/model/friend.h"
#include "src/friendlist.h"
#include "src/model/group.h"
#include "src/widget/chatformheader.h"
#include "src/widget/flowlayout.h"
#include "src/widget/form/chatform.h"
#include "src/widget/groupwidget.h"
#include "src/widget/maskablepixmapwidget.h"
#include "src/widget/style.h"
#include "src/widget/tool/croppinglabel.h"
#include "src/widget/translator.h"
#include "src/persistence/settings.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QTimer>
#include <QToolButton>
#include <QPushButton>

namespace
{
const auto LABEL_PEER_TYPE_OUR = QVariant(QStringLiteral("our"));
const auto LABEL_PEER_TYPE_MUTED = QVariant(QStringLiteral("muted"));
const auto LABEL_PEER_PLAYING_AUDIO = QVariant(QStringLiteral("true"));
const auto LABEL_PEER_NOT_PLAYING_AUDIO = QVariant(QStringLiteral("false"));
const auto PEER_LABEL_STYLE_SHEET_PATH = QStringLiteral("chatArea/chatHead.css");
}

/**
 * @brief Edit name for correct representation if it is needed
 * @param name Editing string
 * @return Source name if it does not contain any newline character, otherwise it chops characters
 * starting with first newline character and appends "..."
 */
QString editName(const QString& name)
{
    const int pos = name.indexOf(QRegularExpression(QStringLiteral("[\n\r]")));
    if (pos == -1) {
        return name;
    }

    QString result = name;
    const int len = result.length();
    result.chop(len - pos);
    result.append(QStringLiteral("…")); // \u2026 Unicode symbol, not just three separate dots
    return result;
}

/**
 * @var QList<QLabel*> GroupChatForm::peerLabels
 * @brief Maps peernumbers to the QLabels in namesListLayout.
 *
 * @var QMap<int, QTimer*> GroupChatForm::peerAudioTimers
 * @brief Timeout = peer stopped sending audio.
 */

GroupChatForm::GroupChatForm(Core& _core, Group* chatGroup, IChatLog& chatLog, IMessageDispatcher& messageDispatcher)
    : GenericChatForm(_core, chatGroup, chatLog, messageDispatcher)
    , core{_core}
    , group(chatGroup)
    , inCall(false)
{
    nusersLabel = new QLabel();

    tabber = new TabCompleter(msgEdit, group);

    fileButton->setEnabled(false);
    fileButton->setProperty("state", "");
    ChatFormHeader::Mode mode = ChatFormHeader::Mode::None;
    if (group->isAvGroupchat()) {
        mode = ChatFormHeader::Mode::Audio;
    }

    headWidget->setMode(mode);
    setName(group->getName());

    nusersLabel->setFont(Style::getFont(Style::Medium));
    nusersLabel->setObjectName("statusLabel");
    retranslateUi();

    const QSize& size = headWidget->getAvatarSize();
    headWidget->setAvatar(Style::scaleSvgImage(":/img/group_dark.svg", size.width(), size.height()));

    msgEdit->setObjectName("group");

    namesListLayout = new FlowLayout(0, 5, 0);
    headWidget->addWidget(nusersLabel);
    headWidget->addLayout(namesListLayout);
    headWidget->addStretch();

    //nameLabel->setMinimumHeight(12);
    nusersLabel->setMinimumHeight(12);

    connect(msgEdit, &ChatTextEdit::tabPressed, tabber, &TabCompleter::complete);
    connect(msgEdit, &ChatTextEdit::keyPressed, tabber, &TabCompleter::reset);
    connect(headWidget, &ChatFormHeader::callTriggered, this, &GroupChatForm::onCallClicked);
    connect(headWidget, &ChatFormHeader::micMuteToggle, this, &GroupChatForm::onMicMuteToggle);
    connect(headWidget, &ChatFormHeader::volMuteToggle, this, &GroupChatForm::onVolMuteToggle);
    connect(headWidget, &ChatFormHeader::nameChanged, chatGroup, &Group::setName);
    connect(group, &Group::titleChanged, this, &GroupChatForm::onTitleChanged);
    connect(group, &Group::userJoined, this, &GroupChatForm::onUserJoined);
    connect(group, &Group::userLeft, this, &GroupChatForm::onUserLeft);
    connect(group, &Group::peerNameChanged, this, &GroupChatForm::onPeerNameChanged);
    connect(group, &Group::numPeersChanged, this, &GroupChatForm::updateUserCount);
    connect(&Settings::getInstance(), &Settings::blackListChanged, this, &GroupChatForm::updateUserNames);

    updateUserNames();
    setAcceptDrops(true);
    Translator::registerHandler(std::bind(&GroupChatForm::retranslateUi, this), this);
}

GroupChatForm::~GroupChatForm()
{
    Translator::unregister(this);
}

void GroupChatForm::onTitleChanged(const QString& author, const QString& title)
{
    if (author.isEmpty()) {
        return;
    }

    const QDateTime curTime = QDateTime::currentDateTime();
    addSystemInfoMessage(curTime, SystemMessageType::titleChanged, {author, title});
}

void GroupChatForm::onScreenshotClicked()
{
    // Unsupported
}

void GroupChatForm::onAttachClicked()
{
    // Unsupported
}

/**
 * @brief Updates user names' labels at the top of group chat
 */
void GroupChatForm::updateUserNames()
{
    QLayoutItem* child;
    while ((child = namesListLayout->takeAt(0))) {
        child->widget()->hide();
        delete child->widget();
        delete child;
    }

    peerLabels.clear();
    const auto peers = group->getPeerList();

    // no need to do anything without any peers
    if (peers.isEmpty()) {
        return;
    }

    /* we store the peer labels by their ToxPk, but the namelist layout
     * needs it in alphabetical order, so we first create and store the labels
     * and then sort them by their text and add them to the layout in that order */
    const auto selfPk = core.getSelfPublicKey();
    for (const auto& peerPk : peers.keys()) {
        const QString peerName = peers.value(peerPk);
        const QString editedName = editName(peerName);
        QLabel* const label = new QLabel(editedName + QLatin1String(", "));
        if (editedName != peerName) {
            label->setToolTip(peerName + " (" + peerPk.toString() + ")");
        } else if (peerName != peerPk.toString()) {
            label->setToolTip(peerPk.toString());
        } // else their name is just their Pk, no tooltip needed
        label->setTextFormat(Qt::PlainText);
        label->setContextMenuPolicy(Qt::CustomContextMenu);

        const Settings& s = Settings::getInstance();
        connect(label, &QLabel::customContextMenuRequested, this, &GroupChatForm::onLabelContextMenuRequested);

        if (peerPk == selfPk) {
            label->setProperty("peerType", LABEL_PEER_TYPE_OUR);
        } else if (s.getBlackList().contains(peerPk.toString())) {
            label->setProperty("peerType", LABEL_PEER_TYPE_MUTED);
        }

        label->setStyleSheet(Style::getStylesheet(PEER_LABEL_STYLE_SHEET_PATH));
        peerLabels.insert(peerPk, label);
    }

    // add the labels in alphabetical order into the layout
    auto nickLabelList = peerLabels.values();

    std::sort(nickLabelList.begin(), nickLabelList.end(), [](const QLabel* a, const QLabel* b)
    {
        return a->text().toLower() < b->text().toLower();
    });

    // remove comma from last sorted label
    QLabel* const lastLabel = nickLabelList.last();
    QString labelText = lastLabel->text();
    labelText.chop(2);
    lastLabel->setText(labelText);
    for (QLabel* l : nickLabelList) {
        namesListLayout->addWidget(l);
    }
}

void GroupChatForm::onUserJoined(const ToxPk& user, const QString& name)
{
    addSystemInfoMessage(QDateTime::currentDateTime(), SystemMessageType::userJoinedGroup, {name});
    updateUserNames();
}

void GroupChatForm::onUserLeft(const ToxPk& user, const QString& name)
{
    addSystemInfoMessage(QDateTime::currentDateTime(), SystemMessageType::userLeftGroup, {name});
    updateUserNames();
}

void GroupChatForm::onPeerNameChanged(const ToxPk& peer, const QString& oldName, const QString& newName)
{
    addSystemInfoMessage(QDateTime::currentDateTime(), SystemMessageType::peerNameChanged,
                         {oldName, newName});
    updateUserNames();
}

void GroupChatForm::peerAudioPlaying(ToxPk peerPk)
{
    peerLabels[peerPk]->setProperty("playingAudio", LABEL_PEER_PLAYING_AUDIO);
    peerLabels[peerPk]->style()->unpolish(peerLabels[peerPk]);
    peerLabels[peerPk]->style()->polish(peerLabels[peerPk]);
    // TODO(sudden6): check if this can ever be false, cause [] default constructs
    if (!peerAudioTimers[peerPk]) {
        peerAudioTimers[peerPk] = new QTimer(this);
        peerAudioTimers[peerPk]->setSingleShot(true);
        connect(peerAudioTimers[peerPk], &QTimer::timeout, [this, peerPk] {
            auto it = peerLabels.find(peerPk);
            if (it != peerLabels.end()) {
                peerLabels[peerPk]->setProperty("playingAudio", LABEL_PEER_NOT_PLAYING_AUDIO);
                peerLabels[peerPk]->style()->unpolish(peerLabels[peerPk]);
                peerLabels[peerPk]->style()->polish(peerLabels[peerPk]);
            }
            delete peerAudioTimers[peerPk];
            peerAudioTimers[peerPk] = nullptr;
        });
    }

    peerLabels[peerPk]->setStyleSheet(Style::getStylesheet(PEER_LABEL_STYLE_SHEET_PATH));
    peerAudioTimers[peerPk]->start(500);
}

void GroupChatForm::dragEnterEvent(QDragEnterEvent* ev)
{
    if (!ev->mimeData()->hasFormat("toxPk")) {
        return;
    }
    ToxPk toxPk{ev->mimeData()->data("toxPk")};
    Friend* frnd = FriendList::findFriend(toxPk);
    if (frnd)
        ev->acceptProposedAction();
}

void GroupChatForm::dropEvent(QDropEvent* ev)
{
    if (!ev->mimeData()->hasFormat("toxPk")) {
        return;
    }
    ToxPk toxPk{ev->mimeData()->data("toxPk")};
    Friend* frnd = FriendList::findFriend(toxPk);
    if (!frnd)
        return;

    int friendId = frnd->getId();
    int groupId = group->getId();
    if (Status::isOnline(frnd->getStatus())) {
        core.groupInviteFriend(friendId, groupId);
    }
}

void GroupChatForm::onMicMuteToggle()
{
    if (audioInputFlag) {
        CoreAV* av = core.getAv();
        const bool oldMuteState = av->isGroupCallInputMuted(group);
        const bool newMute = !oldMuteState;
        av->muteCallInput(group, newMute);
        headWidget->updateMuteMicButton(inCall, newMute);
    }
}

void GroupChatForm::onVolMuteToggle()
{
    if (audioOutputFlag) {
        CoreAV* av = core.getAv();
        const bool oldMuteState = av->isGroupCallOutputMuted(group);
        const bool newMute = !oldMuteState;
        av->muteCallOutput(group, newMute);
        headWidget->updateMuteVolButton(inCall, newMute);
    }
}

void GroupChatForm::onCallClicked()
{
    CoreAV* av = core.getAv();

    if (!inCall) {
        joinGroupCall();
    } else {
        leaveGroupCall();
    }

    headWidget->updateCallButtons(true, inCall);

    const bool inMute = av->isGroupCallInputMuted(group);
    headWidget->updateMuteMicButton(inCall, inMute);

    const bool outMute = av->isGroupCallOutputMuted(group);
    headWidget->updateMuteVolButton(inCall, outMute);
}

void GroupChatForm::keyPressEvent(QKeyEvent* ev)
{
    // Push to talk (CTRL+P)
    if (ev->key() == Qt::Key_P && (ev->modifiers() & Qt::ControlModifier) && inCall) {
        onMicMuteToggle();
    }

    if (msgEdit->hasFocus())
        return;
}

void GroupChatForm::keyReleaseEvent(QKeyEvent* ev)
{
    // Push to talk (CTRL+P)
    if (ev->key() == Qt::Key_P && (ev->modifiers() & Qt::ControlModifier) && inCall) {
        onMicMuteToggle();
    }

    if (msgEdit->hasFocus())
        return;
}

/**
 * @brief Updates users' count label text
 */
void GroupChatForm::updateUserCount(int numPeers)
{
    nusersLabel->setText(tr("%n user(s) in chat", "Number of users in chat", numPeers));
    headWidget->updateCallButtons(true, inCall);
}

void GroupChatForm::retranslateUi()
{
    updateUserCount(group->getPeersCount());
}

void GroupChatForm::onLabelContextMenuRequested(const QPoint& localPos)
{
    QLabel* label = static_cast<QLabel*>(QObject::sender());

    if (label == nullptr) {
        return;
    }

    const QPoint pos = label->mapToGlobal(localPos);
    const QString muteString = tr("mute");
    const QString unmuteString = tr("unmute");
    Settings& s = Settings::getInstance();
    QStringList blackList = s.getBlackList();
    QMenu* const contextMenu = new QMenu(this);
    const ToxPk selfPk = core.getSelfPublicKey();
    ToxPk peerPk;

    // delete menu after it stops being used
    connect(contextMenu, &QMenu::aboutToHide, contextMenu, &QObject::deleteLater);

    peerPk = peerLabels.key(label);
    if (peerPk.isEmpty() || peerPk == selfPk) {
        return;
    }

    const bool isPeerBlocked = blackList.contains(peerPk.toString());
    QString menuTitle = label->text();
    if (menuTitle.endsWith(QLatin1String(", "))) {
        menuTitle.chop(2);
    }
    QAction* menuTitleAction = contextMenu->addAction(menuTitle);
    menuTitleAction->setEnabled(false); // make sure the title is not clickable
    contextMenu->addSeparator();

    const QAction* toggleMuteAction;
    if (isPeerBlocked) {
        toggleMuteAction = contextMenu->addAction(unmuteString);
    } else {
        toggleMuteAction = contextMenu->addAction(muteString);
    }
    contextMenu->setStyleSheet(Style::getStylesheet(PEER_LABEL_STYLE_SHEET_PATH));

    const QAction* selectedItem = contextMenu->exec(pos);
    if (selectedItem == toggleMuteAction) {
        if (isPeerBlocked) {
            const int index = blackList.indexOf(peerPk.toString());
            if (index != -1) {
                blackList.removeAt(index);
            }
        } else {
            blackList << peerPk.toString();
        }

        s.setBlackList(blackList);
    }
}

void GroupChatForm::joinGroupCall()
{
    CoreAV* av = core.getAv();
    av->joinGroupCall(*group);
    audioInputFlag = true;
    audioOutputFlag = true;
    inCall = true;
}

void GroupChatForm::leaveGroupCall()
{
    CoreAV* av = core.getAv();
    av->leaveGroupCall(group->getId());
    audioInputFlag = false;
    audioOutputFlag = false;
    inCall = false;
}
