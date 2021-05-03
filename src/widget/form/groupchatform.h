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

#include "genericchatform.h"
#include "src/core/toxpk.h"
#include "src/core/toxcall.h"
#include "src/core/toxpk.h"
#include <QMap>

namespace Ui {
class MainWindow;
}
class Group;
class TabCompleter;
class FlowLayout;
class QTimer;
class GroupId;
class IMessageDispatcher;
struct Message;

class GroupChatForm : public GenericChatForm
{
    Q_OBJECT
public:
    explicit GroupChatForm(Core& _core, Group* chatGroup, IChatLog& chatLog, IMessageDispatcher& messageDispatcher);
    ~GroupChatForm();

    void peerAudioPlaying(ToxPk peerPk);

private slots:
    void onScreenshotClicked() override;
    void onAttachClicked() override;
    void onMicMuteToggle();
    void onVolMuteToggle();
    void onCallClicked();
    void onUserJoined(const ToxPk& user, const QString& name);
    void onUserLeft(const ToxPk& user, const QString& name);
    void onPeerNameChanged(const ToxPk& peer, const QString& oldName, const QString& newName);
    void onTitleChanged(const QString& author, const QString& title);
    void onLabelContextMenuRequested(const QPoint& localPos);

protected:
    void keyPressEvent(QKeyEvent* ev) final;
    void keyReleaseEvent(QKeyEvent* ev) final;
    // drag & drop
    void dragEnterEvent(QDragEnterEvent* ev) final;
    void dropEvent(QDropEvent* ev) final;

private:
    void retranslateUi();
    void updateUserCount(int numPeers);
    void updateUserNames();
    void joinGroupCall();
    void leaveGroupCall();

private:
    Core& core;
    Group* group;
    QMap<ToxPk, QLabel*> peerLabels;
    QMap<ToxPk, QTimer*> peerAudioTimers;
    FlowLayout* namesListLayout;
    QLabel* nusersLabel;
    TabCompleter* tabber;
    std::unique_ptr<ToxGroupCall> call;
};
