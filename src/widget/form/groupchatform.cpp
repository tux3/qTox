/*
    Copyright © 2014-2015 by The qTox Project

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

#include <QDragEnterEvent>
#include <QMimeData>
#include <QTimer>

#include "tabcompleter.h"
#include "group.h"
#include "friend.h"
#include "friendlist.h"
#include "widget/groupwidget.h"
#include "widget/tool/croppinglabel.h"
#include "widget/maskablepixmapwidget.h"
#include "core/core.h"
#include "core/coreav.h"
#include "widget/style.h"
#include "widget/flowlayout.h"
#include "widget/translator.h"
#include "widget/form/chatform.h"
#include "video/groupnetcamview.h"

/**
 * @var QList<QLabel*> GroupChatForm::peerLabels
 * @brief Maps peernumbers to the QLabels in namesListLayout.
 *
 * @var QMap<int, QTimer*> GroupChatForm::peerAudioTimers
 * @brief Timeout = peer stopped sending audio.
 */

GroupChatForm::GroupChatForm(Group* chatGroup)
    : audioInputFlag(false)
    , audioOutputFlag(false)
    , group(chatGroup)
    , inCall(false)
{
    nusersLabel = new QLabel();

    tabber = new TabCompleter(msgEdit, group);

    fileButton->setEnabled(false);
    if (group->isAvGroupchat())
    {
        videoButton->setEnabled(false);
        videoButton->setObjectName("grey");
    }
    else
    {
        videoButton->setVisible(false);
        callButton->setVisible(false);
        volButton->setVisible(false);
        micButton->setVisible(false);
    }

    nameLabel->setText(group->getGroupWidget()->getName());

    nusersLabel->setFont(Style::getFont(Style::Medium));
    nusersLabel->setObjectName("statusLabel");
    retranslateUi();

    avatar->setPixmap(Style::scaleSvgImage(":/img/group_dark.svg", avatar->width(), avatar->height()));

    msgEdit->setObjectName("group");

    namesListLayout = new FlowLayout(0,5,0);
    QStringList names(group->getPeerList());

    for (QString& name : names)
    {
        QLabel *l = new QLabel();
        QString tooltip = correctNames(name);
        if (tooltip.isNull())
        {
            l->setToolTip(tooltip);
        }
        l->setText(name);
        l->setTextFormat(Qt::PlainText);
        namesListLayout->addWidget(l);
    }

    headTextLayout->addWidget(nusersLabel);
    headTextLayout->addLayout(namesListLayout);
    headTextLayout->addStretch();

    nameLabel->setMinimumHeight(12);
    nusersLabel->setMinimumHeight(12);

    connect(sendButton, SIGNAL(clicked()), this, SLOT(onSendTriggered()));
    connect(msgEdit, SIGNAL(enterPressed()), this, SLOT(onSendTriggered()));
    connect(msgEdit, &ChatTextEdit::tabPressed, tabber, &TabCompleter::complete);
    connect(msgEdit, &ChatTextEdit::keyPressed, tabber, &TabCompleter::reset);
    connect(callButton, &QPushButton::clicked, this, &GroupChatForm::onCallClicked);
    connect(micButton, SIGNAL(clicked()), this, SLOT(onMicMuteToggle()));
    connect(volButton, SIGNAL(clicked()), this, SLOT(onVolMuteToggle()));
    connect(nameLabel, &CroppingLabel::editFinished, this, [=](const QString& newName)
    {
        if (!newName.isEmpty())
        {
            nameLabel->setText(newName);
            emit groupTitleChanged(group->getGroupId(), newName.left(128));
        }
    });

    setAcceptDrops(true);
    Translator::registerHandler(std::bind(&GroupChatForm::retranslateUi, this), this);
}

// Correct names with "\n" in NamesListLayout widget
QString GroupChatForm::correctNames(QString& name)
{
    int pos = name.indexOf(QRegExp("\n|\r\n|\r"));
    int len = name.length();
    if ( (pos < len) && (pos !=-1) )
    {
        QString tooltip = name;
        name.remove( pos, len-pos );
        name.append("...");
        return tooltip;
    }
    else
    {
        return QString();
    }
}

GroupChatForm::~GroupChatForm()
{
    Translator::unregister(this);
}

void GroupChatForm::onSendTriggered()
{
    QString msg = msgEdit->toPlainText();
    if (msg.isEmpty())
        return;

    msgEdit->setLastMessage(msg);
    msgEdit->clear();

    if (group->getPeersCount() != 1)
    {
        if (msg.startsWith(ChatForm::ACTION_PREFIX, Qt::CaseInsensitive))
        {
            msg.remove(0, ChatForm::ACTION_PREFIX.length());
            emit sendAction(group->getGroupId(), msg);
        }
        else
        {
            emit sendMessage(group->getGroupId(), msg);
        }
    }
    else
    {
        if (msg.startsWith(ChatForm::ACTION_PREFIX, Qt::CaseInsensitive))
            addSelfMessage(msg.mid(ChatForm::ACTION_PREFIX.length()), true, QDateTime::currentDateTime(), true);
        else
            addSelfMessage(msg, false, QDateTime::currentDateTime(), true);
    }
}

void GroupChatForm::onUserListChanged()
{
    int peersCount = group->getPeersCount();
    if (peersCount == 1)
        nusersLabel->setText(tr("1 user in chat", "Number of users in chat"));
    else
        nusersLabel->setText(tr("%1 users in chat", "Number of users in chat").arg(peersCount));

    QLayoutItem *child;
    while ((child = namesListLayout->takeAt(0)))
    {
        child->widget()->hide();
        delete child->widget();
        delete child;
    }
    peerLabels.clear();

    // the list needs peers in peernumber order, nameLayout needs alphabetical
    QList<QLabel*> nickLabelList;

    // first traverse in peer number order, storing the QLabels as necessary
    QStringList names = group->getPeerList();
    unsigned nNames = names.size();
    for (unsigned i=0; i<nNames; ++i)
    {
        QString tooltip = correctNames(names[i]);
        peerLabels.append(new QLabel(names[i]));
        if (!tooltip.isEmpty())
            peerLabels[i]->setToolTip(tooltip);
        peerLabels[i]->setTextFormat(Qt::PlainText);
        nickLabelList.append(peerLabels[i]);
        if (group->isSelfPeerNumber(i))
            peerLabels[i]->setStyleSheet("QLabel {color : green;}");

        if (netcam && !group->isSelfPeerNumber(i))
            static_cast<GroupNetCamView*>(netcam)->addPeer(i, names[i]);
    }

    if (netcam)
        static_cast<GroupNetCamView*>(netcam)->clearPeers();

    // now alphabetize and add to layout
    qSort(nickLabelList.begin(), nickLabelList.end(), [](QLabel *a, QLabel *b){return a->text().toLower() < b->text().toLower();});
    for (unsigned i=0; i<nNames; ++i)
    {
        QLabel *label = nickLabelList.at(i);
        if (i != nNames - 1)
            label->setText(label->text() + ", ");

        namesListLayout->addWidget(label);
    }

    // Enable or disable call button
    if (peersCount > 1 && group->isAvGroupchat())
    {
        // don't set button to green if call running
        if (!inCall)
        {
            callButton->setEnabled(true);
            callButton->setObjectName("green");
            callButton->style()->polish(callButton);
            callButton->setToolTip(tr("Start audio call"));
        }
    }
    else
    {
        callButton->setEnabled(false);
        callButton->setObjectName("grey");
        callButton->style()->polish(callButton);
        callButton->setToolTip("");
        Core::getInstance()->getAv()->leaveGroupCall(group->getGroupId());
        hideNetcam();
    }
}

void GroupChatForm::peerAudioPlaying(int peer)
{
    peerLabels[peer]->setStyleSheet("QLabel {color : red;}");
    if (!peerAudioTimers[peer])
    {
        peerAudioTimers[peer] = new QTimer(this);
        peerAudioTimers[peer]->setSingleShot(true);
        connect(peerAudioTimers[peer], &QTimer::timeout, [this, peer]
        {
            if (netcam)
                static_cast<GroupNetCamView*>(netcam)->removePeer(peer);

            if (peer >= peerLabels.size())
                return;

            peerLabels[peer]->setStyleSheet("");
            delete peerAudioTimers[peer];
            peerAudioTimers[peer] = nullptr;
        });

        if (netcam)
        {
            static_cast<GroupNetCamView*>(netcam)->removePeer(peer);
            static_cast<GroupNetCamView*>(netcam)->addPeer(peer, group->getPeerList()[peer]);
        }
    }
    peerAudioTimers[peer]->start(500);
}

void GroupChatForm::dragEnterEvent(QDragEnterEvent *ev)
{
    ToxId toxId = ToxId(ev->mimeData()->text());
    Friend *frnd = FriendList::findFriend(toxId);
    if (frnd)
        ev->acceptProposedAction();
}

void GroupChatForm::dropEvent(QDropEvent *ev)
{
    ToxId toxId = ToxId(ev->mimeData()->text());
    Friend *frnd = FriendList::findFriend(toxId);
    if (!frnd)
        return;

    int friendId = frnd->getFriendID();
    int groupId = group->getGroupId();
    Core::getInstance()->groupInviteFriend(friendId, groupId);
}

void GroupChatForm::onMicMuteToggle()
{
    if (audioInputFlag)
    {
        CoreAV* av = Core::getInstance()->getAv();
        if (micButton->objectName() == "red")
        {
            av->muteCallInput(group, false);
            micButton->setObjectName("green");
            micButton->setToolTip(tr("Mute microphone"));
        }
        else
        {
            av->muteCallInput(group, true);
            micButton->setObjectName("red");
            micButton->setToolTip(tr("Unmute microphone"));
        }

        Style::repolish(micButton);
    }
}

void GroupChatForm::onVolMuteToggle()
{
    if (audioOutputFlag)
    {
        CoreAV* av = Core::getInstance()->getAv();
        if (volButton->objectName() == "red")
        {
            av->muteCallOutput(group, false);
            volButton->setObjectName("green");
            volButton->setToolTip(tr("Mute call"));
        }
        else
        {
            av->muteCallOutput(group, true);
            volButton->setObjectName("red");
            volButton->setToolTip(tr("Unmute call"));
        }

        Style::repolish(volButton);
    }
}

void GroupChatForm::onCallClicked()
{
    if (!inCall)
    {
        Core::getInstance()->getAv()->joinGroupCall(group->getGroupId());
        audioInputFlag = true;
        audioOutputFlag = true;
        callButton->setObjectName("red");
        callButton->style()->polish(callButton);
        callButton->setToolTip(tr("End audio call"));
        micButton->setObjectName("green");
        micButton->style()->polish(micButton);
        micButton->setToolTip(tr("Mute microphone"));
        volButton->setObjectName("green");
        volButton->style()->polish(volButton);
        volButton->setToolTip(tr("Mute call"));
        inCall = true;
        showNetcam();
    }
    else
    {
        Core::getInstance()->getAv()->leaveGroupCall(group->getGroupId());
        audioInputFlag = false;
        audioOutputFlag = false;
        callButton->setObjectName("green");
        callButton->style()->polish(callButton);
        callButton->setToolTip(tr("Start audio call"));
        micButton->setObjectName("grey");
        micButton->style()->polish(micButton);
        micButton->setToolTip("");
        volButton->setObjectName("grey");
        volButton->style()->polish(volButton);
        volButton->setToolTip("");
        inCall = false;
        hideNetcam();
    }
}

GenericNetCamView *GroupChatForm::createNetcam()
{
    GroupNetCamView* view = new GroupNetCamView(group->getGroupId(), this);

    QStringList names = group->getPeerList();
    for (int i = 0; i<names.size(); ++i)
    {
        if (!group->isSelfPeerNumber(i))
            static_cast<GroupNetCamView*>(view)->addPeer(i, names[i]);
    }

    return view;
}

void GroupChatForm::keyPressEvent(QKeyEvent* ev)
{
    // Push to talk (CTRL+P)
    if (ev->key() == Qt::Key_P && (ev->modifiers() & Qt::ControlModifier) && inCall)
    {
        CoreAV* av = Core::getInstance()->getAv();
        if (!av->isGroupCallInputMuted(group))
        {
            av->muteCallInput(group, false);
            micButton->setObjectName("green");
            micButton->style()->polish(micButton);
            Style::repolish(micButton);
        }
    }

    if (msgEdit->hasFocus())
        return;
}

void GroupChatForm::keyReleaseEvent(QKeyEvent* ev)
{
    // Push to talk (CTRL+P)
    if (ev->key() == Qt::Key_P && (ev->modifiers() & Qt::ControlModifier) && inCall)
    {
        CoreAV* av = Core::getInstance()->getAv();
        if (av->isGroupCallInputMuted(group))
        {
            av->muteCallInput(group, true);
            micButton->setObjectName("red");
            micButton->style()->polish(micButton);
            Style::repolish(micButton);
        }
    }

    if (msgEdit->hasFocus())
        return;
}

void GroupChatForm::retranslateUi()
{
    int peersCount = group->getPeersCount();
    if (peersCount == 1)
        nusersLabel->setText(tr("1 user in chat", "Number of users in chat"));
    else
        nusersLabel->setText(tr("%1 users in chat", "Number of users in chat").arg(peersCount));
}
