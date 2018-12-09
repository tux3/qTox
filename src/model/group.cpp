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

#include "group.h"
#include "friend.h"
#include "src/friendlist.h"
#include "src/core/core.h"
#include "src/persistence/settings.h"
#include "src/widget/form/groupchatform.h"
#include "src/widget/groupwidget.h"
#include <QDebug>

static const int MAX_GROUP_TITLE_LENGTH = 128;

Group::Group(int groupId, const ToxPk persistentGroupId, const QString& name, bool isAvGroupchat, const QString& selfName)
    : selfName{selfName}
    , title{name}
    , groupId(groupId)
    , persistentGroupId{persistentGroupId}
    , avGroupchat{isAvGroupchat}
{
    // in groupchats, we only notify on messages containing your name <-- dumb
    // sound notifications should be on all messages, but system popup notification
    // on naming is appropriate
    hasNewMessages = 0;
    userWasMentioned = 0;
    regeneratePeerList();
}

void Group::updatePeer(int peerId, QString name)
{
    ToxPk peerKey = Core::getInstance()->getGroupPeerPk(groupId, peerId);
    toxpks[peerKey] = name;

    Friend* f = FriendList::findFriend(peerKey);
    if (f != nullptr) {
        // use the displayed name from the friends list
        toxpks[peerKey] = f->getDisplayedName();
    }
    emit userListChanged(groupId, toxpks);
}

void Group::setName(const QString& newTitle)
{
    const QString shortTitle = newTitle.left(MAX_GROUP_TITLE_LENGTH);
    if (!shortTitle.isEmpty() && title != shortTitle) {
        title = shortTitle;
        emit displayedNameChanged(title);
        emit titleChangedByUser(groupId, title);
        emit titleChanged(groupId, selfName, title);
    }
}

void Group::setTitle(const QString& author, const QString& newTitle)
{
    const QString shortTitle = newTitle.left(MAX_GROUP_TITLE_LENGTH);
    if (!shortTitle.isEmpty() && title != shortTitle) {
        title = shortTitle;
        emit displayedNameChanged(title);
        emit titleChanged(groupId, author, title);
    }
}

QString Group::getName() const
{
    return title;
}

QString Group::getDisplayedName() const
{
    return getName();
}

/**
 * @brief performs a peerId to ToxPk lookup
 * @param peerId peerId to lookup
 * @return ToxPk if peerId found
 * @note should not be used, reference peers by their ToxPk instead
 * @todo remove this function
 */
const ToxPk Group::resolvePeerId(int peerId) const
{
    const Core* core = Core::getInstance();
    return core->getGroupPeerPk(groupId, peerId);
}

void Group::regeneratePeerList()
{
    const Core* core = Core::getInstance();

    QStringList peers = core->getGroupPeerNames(groupId);
    toxpks.clear();
    const int nPeers = peers.size();
    for (int i = 0; i < nPeers; ++i) {
        const auto pk = core->getGroupPeerPk(groupId, i);

        toxpks[pk] = peers[i];
        if (toxpks[pk].isEmpty()) {
            toxpks[pk] =
                tr("<Empty>", "Placeholder when someone's name in a group chat is empty");
        }

        Friend* f = FriendList::findFriend(pk);
        if (f != nullptr && f->hasAlias()) {
            toxpks[pk] = f->getDisplayedName();
        }
    }

    emit userListChanged(groupId, toxpks);
}

bool Group::isAvGroupchat() const
{
    return avGroupchat;
}

uint32_t Group::getId() const
{
    return groupId;
}

const ToxPk Group::getPersistentId() const
{
    return persistentGroupId;
}

int Group::getPeersCount() const
{
    return toxpks.size();
}

/**
 * @brief Gets the PKs and names of all peers
 * @return PKs and names of all peers, including our own PK and name
 */
const QMap<ToxPk, QString>& Group::getPeerList() const
{
    return toxpks;
}

void Group::setEventFlag(bool f)
{
    hasNewMessages = f;
}

bool Group::getEventFlag() const
{
    return hasNewMessages;
}

void Group::setMentionedFlag(bool f)
{
    userWasMentioned = f;
}

bool Group::getMentionedFlag() const
{
    return userWasMentioned;
}

QString Group::resolveToxId(const ToxPk& id) const
{
    auto it = toxpks.find(id);

    if (it != toxpks.end()) {
        return *it;
    }

    return QString();
}

void Group::setSelfName(const QString& name)
{
    selfName = name;
}

QString Group::getSelfName() const
{
    return selfName;
}
