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

#include "groupchatform.h"

#include "tabcompleter.h"
#include "src/core/core.h"
#include "src/core/coreav.h"
#include "src/chatlog/chatlog.h"
#include "src/chatlog/content/text.h"
#include "src/model/friend.h"
#include "src/friendlist.h"
#include "src/model/group.h"
#include "src/video/groupnetcamview.h"
#include "src/widget/chatformheader.h"
#include "src/widget/flowlayout.h"
#include "src/widget/form/chatform.h"
#include "src/widget/groupwidget.h"
#include "src/widget/maskablepixmapwidget.h"
#include "src/widget/style.h"
#include "src/widget/tool/croppinglabel.h"
#include "src/widget/translator.h"
#include "src/persistence/settings.h"

#include "src/nexus.h"
#include "src/persistence/profile.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QTimer>
#include <QToolButton>

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

GroupChatForm::GroupChatForm(Group* chatGroup, History* history)
    : GenericChatForm (chatGroup, history)
    , group(chatGroup)
    , inCall(false)
    , history(history)
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

    connect(sendButton, SIGNAL(clicked()), this, SLOT(onSendTriggered()));
    connect(msgEdit, SIGNAL(enterPressed()), this, SLOT(onSendTriggered()));
    connect(msgEdit, &ChatTextEdit::tabPressed, tabber, &TabCompleter::complete);
    connect(msgEdit, &ChatTextEdit::keyPressed, tabber, &TabCompleter::reset);
    connect(headWidget, &ChatFormHeader::callTriggered, this, &GroupChatForm::onCallClicked);
    connect(headWidget, &ChatFormHeader::micMuteToggle, this, &GroupChatForm::onMicMuteToggle);
    connect(headWidget, &ChatFormHeader::volMuteToggle, this, &GroupChatForm::onVolMuteToggle);
    connect(headWidget, &ChatFormHeader::nameChanged, chatGroup, &Group::setName);
    connect(group, &Group::userListChanged, this, &GroupChatForm::onUserListChanged);
    connect(group, &Group::titleChanged, this, &GroupChatForm::onTitleChanged);
    connect(&Settings::getInstance(), &Settings::blackListChanged, this, &GroupChatForm::updateUserNames);

    onUserListChanged();
    if (Nexus::getProfile()->isHistoryEnabled()) {
        loadHistoryDefaultNum(true);
    }
    setAcceptDrops(true);
    Translator::registerHandler(std::bind(&GroupChatForm::retranslateUi, this), this);
}

GroupChatForm::~GroupChatForm()
{
    Translator::unregister(this);
}

void GroupChatForm::onSendTriggered()
{
    sendMessageStr(msgEdit->toPlainText());
    msgEdit->clear();
}

void GroupChatForm::sendMessageStr(QString msg)
{
    if (msg.isEmpty()) {
        return;
    }

    bool isAction = msg.startsWith(ChatForm::ACTION_PREFIX, Qt::CaseInsensitive);
    if (isAction) {
        msg.remove(0, ChatForm::ACTION_PREFIX.length());
    }

    QStringList splittedMsg = Core::splitMessage(msg, tox_max_message_length());
    QDateTime timestamp = QDateTime::currentDateTime();

    for (const QString& part : splittedMsg) {
        QString historyPart = part;
        if (isAction) {
            historyPart = ChatForm::ACTION_PREFIX + part;
        }

        Core* core = Core::getInstance();
        uint32_t groupId = group->getId();
        if (group->getPeersCount() != 1) {
            isAction ? core->sendGroupAction(groupId, part) : core->sendGroupMessage(groupId, part);
        } else {
            // NOTE: we are considered our own group peer by toxcore, so will receive our own message, so we only add
            // the message ourselves if we're not sending it.
            addSelfMessage(part, timestamp, isAction);
            if (history && Settings::getInstance().getEnableLogging()) {
                ToxPk selfPk = Core::getInstance()->getSelfPublicKey();
                ToxPk persistentId = group->getPersistentId();
                QString name = Core::getInstance()->getUsername();
                history->addNewMessage(persistentId, historyPart, selfPk, timestamp, true, name, nullptr);
            }
        }

        // set last message only when sending it
        msgEdit->setLastMessage(msg);
    }
}

/**
 * @brief This slot is intended to connect to Group::userListChanged signal.
 * Brief list of actions made by slot:
 *      1) sets text of how many people are in the group;
 *      2) creates lexicographically sorted comma-separated list of user names, each name in its own
 *      label;
 *      3) sets call button style depending on peer count and etc.
 */
void GroupChatForm::onUserListChanged()
{
    updateUserCount();
    updateUserNames();

    // Enable or disable call button
    const int peersCount = group->getPeersCount();
    const bool online = peersCount > 1;
    headWidget->updateCallButtons(online, inCall);
    if (!online || !group->isAvGroupchat()) {
        Core::getInstance()->getAv()->leaveGroupCall(group->getId());
        hideNetcam();
    }
}

void GroupChatForm::onTitleChanged(uint32_t groupId, const QString& author, const QString& title)
{
    Q_UNUSED(groupId);
    if (author.isEmpty()) {
        return;
    }

    const QString message = tr("%1 has set the title to %2").arg(author, title);
    const QDateTime curTime = QDateTime::currentDateTime();
    addSystemInfoMessage(message, ChatMessage::INFO, curTime);
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
    const auto selfPk = Core::getInstance()->getSelfPublicKey();
    for (const auto& peerPk : peers.keys()) {
        const QString fullName = peers.value(peerPk);
        const QString editedName = editName(fullName).append(QLatin1String(", "));
        QLabel* const label = new QLabel(editedName);
        if (editedName != fullName) {
            label->setToolTip(fullName);
        }
        label->setTextFormat(Qt::PlainText);
        label->setContextMenuPolicy(Qt::CustomContextMenu);

        const Settings& s = Settings::getInstance();
        connect(label, &QLabel::customContextMenuRequested, this, &GroupChatForm::onLabelContextMenuRequested);

        if (peerPk == selfPk) {
            label->setProperty("peerType", LABEL_PEER_TYPE_OUR);
        } else if (s.getBlackList().contains(peerPk.toString())) {
            label->setProperty("peerType", LABEL_PEER_TYPE_MUTED);
        } else if (netcam != nullptr) {
            static_cast<GroupNetCamView*>(netcam)->addPeer(peerPk, fullName);
        }

        label->setStyleSheet(Style::getStylesheet(PEER_LABEL_STYLE_SHEET_PATH));
        peerLabels.insert(peerPk, label);
    }

    if (netcam != nullptr) {
        static_cast<GroupNetCamView*>(netcam)->clearPeers();
    }

    // add the labels in alphabetical order into the layout
    auto nickLabelList = peerLabels.values();

    qSort(nickLabelList.begin(), nickLabelList.end(), [](const QLabel* a, const QLabel* b)
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

void GroupChatForm::peerAudioPlaying(ToxPk peerPk)
{
    peerLabels[peerPk]->setProperty("playingAudio", LABEL_PEER_PLAYING_AUDIO);
    // TODO(sudden6): check if this can ever be false, cause [] default constructs
    if (!peerAudioTimers[peerPk]) {
        peerAudioTimers[peerPk] = new QTimer(this);
        peerAudioTimers[peerPk]->setSingleShot(true);
        connect(peerAudioTimers[peerPk], &QTimer::timeout, [this, peerPk] {
            if (netcam) {
                static_cast<GroupNetCamView*>(netcam)->removePeer(peerPk);
            }

            peerLabels[peerPk]->setProperty("playingAudio", LABEL_PEER_NOT_PLAYING_AUDIO);
            delete peerAudioTimers[peerPk];
            peerAudioTimers[peerPk] = nullptr;
        });

        if (netcam) {
            static_cast<GroupNetCamView*>(netcam)->removePeer(peerPk);
            const auto nameIt = group->getPeerList().find(peerPk);
            static_cast<GroupNetCamView*>(netcam)->addPeer(peerPk, nameIt.value());
        }
    }

    peerLabels[peerPk]->setStyleSheet(Style::getStylesheet(PEER_LABEL_STYLE_SHEET_PATH));
    peerAudioTimers[peerPk]->start(500);
}

void GroupChatForm::dragEnterEvent(QDragEnterEvent* ev)
{
    ToxId toxId = ToxId(ev->mimeData()->text());
    Friend* frnd = FriendList::findFriend(toxId.getPublicKey());
    if (frnd)
        ev->acceptProposedAction();
}

void GroupChatForm::dropEvent(QDropEvent* ev)
{
    ToxId toxId = ToxId(ev->mimeData()->text());
    Friend* frnd = FriendList::findFriend(toxId.getPublicKey());
    if (!frnd)
        return;

    int friendId = frnd->getId();
    int groupId = group->getId();
    if (frnd->getStatus() != Status::Offline) {
        Core::getInstance()->groupInviteFriend(friendId, groupId);
    }
}

void GroupChatForm::onMicMuteToggle()
{
    if (audioInputFlag) {
        CoreAV* av = Core::getInstance()->getAv();
        const bool oldMuteState = av->isGroupCallInputMuted(group);
        const bool newMute = !oldMuteState;
        av->muteCallInput(group, newMute);
        headWidget->updateMuteMicButton(inCall, newMute);
    }
}

void GroupChatForm::onVolMuteToggle()
{
    if (audioOutputFlag) {
        CoreAV* av = Core::getInstance()->getAv();
        const bool oldMuteState = av->isGroupCallOutputMuted(group);
        const bool newMute = !oldMuteState;
        av->muteCallOutput(group, newMute);
        headWidget->updateMuteVolButton(inCall, newMute);
    }
}

void GroupChatForm::onCallClicked()
{
    CoreAV* av = Core::getInstance()->getAv();
    if (!inCall) {
        av->joinGroupCall(group->getId());
        audioInputFlag = true;
        audioOutputFlag = true;
        inCall = true;
        showNetcam();
    } else {
        av->leaveGroupCall(group->getId());
        audioInputFlag = false;
        audioOutputFlag = false;
        inCall = false;
        hideNetcam();
    }

    const int peersCount = group->getPeersCount();
    const bool online = peersCount > 1;
    headWidget->updateCallButtons(online, inCall);

    const bool inMute = av->isGroupCallInputMuted(group);
    headWidget->updateMuteMicButton(inCall, inMute);

    const bool outMute = av->isGroupCallOutputMuted(group);
    headWidget->updateMuteVolButton(inCall, outMute);
}

GenericNetCamView* GroupChatForm::createNetcam()
{
    GroupNetCamView* view = new GroupNetCamView(group->getId(), this);

    const auto& names = group->getPeerList();
    const auto ownPk = Core::getInstance()->getSelfPublicKey();
    for (const auto& peerPk : names.keys()) {
        if (peerPk != ownPk) {
            static_cast<GroupNetCamView*>(view)->addPeer(peerPk, names.find(peerPk).value());
        }
    }

    return view;
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
void GroupChatForm::updateUserCount()
{
    const int peersCount = group->getPeersCount();
    if (peersCount == 1) {
        nusersLabel->setText(tr("1 user in chat", "Number of users in chat"));
    } else {
        nusersLabel->setText(tr("%1 users in chat", "Number of users in chat").arg(peersCount));
    }
}

void GroupChatForm::retranslateUi()
{
    updateUserCount();
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
    const ToxPk selfPk = Core::getInstance()->getSelfPublicKey();
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
