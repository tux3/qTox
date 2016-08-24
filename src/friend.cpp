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


#include "friend.h"

#include "src/core/core.h"
#include "src/persistence/settings.h"
#include "src/persistence/profile.h"
#include "src/nexus.h"
#include "src/grouplist.h"
#include "src/group.h"

Friend::Friend(uint32_t FriendId, const ToxId &UserId)
    : userName{Core::getInstance()->getPeerName(UserId)}
    , userAlias(Settings::getInstance().getFriendAlias(UserId))
    , userID(UserId)
    , friendId(FriendId)
    , hasNewEvents(0)
    , friendStatus(Status::Offline)
    , offlineEngine(this)
{
    if (userName.isEmpty())
        userName = UserId.publicKey;
}

/**
 * @brief Loads the friend's chat history if enabled.
 */
void Friend::loadHistory()
{
    if (Nexus::getProfile()->isHistoryEnabled())
        emit loadChatHistory();
}

/**
 * @brief Friend::setName
 * @param name New name to friend.
 *
 * Change the real username of friend.
 */
void Friend::setName(QString name)
{
    if (name.isEmpty())
        name = userID.publicKey;

    if (userName != name)
    {
        userName = name;
        emit nameChanged(userName);
    }
}

/**
 * @brief Friend::setAlias
 * @param alias New alias to friend.
 *
 * Set new displayed name to friend.
 * Alias will override friend name in friend list.
 */
void Friend::setAlias(QString alias)
{
    if (userAlias != alias)
    {
        userAlias = alias;
        emit displayedNameChanged(friendId);
    }
}

/**
 * @brief Friend::setStatusMessage
 * @param message New status message.
 *
 * Change showed friend status message.
 */
void Friend::setStatusMessage(QString message)
{
    if (statusMessage != message)
    {
        statusMessage = message;
        emit newStatusMessage(message);
    }
}

/**
 * @brief Friend::getStatusMessage
 * @return Friend status message.
 */
QString Friend::getStatusMessage()
{
    return statusMessage;
}

/**
 * @brief Friend::getDisplayedName
 * @return Friend displayed name.
 *
 * Return friend alias if setted, username otherwise.
 */
QString Friend::getDisplayedName() const
{
    return userAlias.isEmpty() ? userName : userAlias;
}

/**
 * @brief Friend::hasAlias
 * @return True, if user sets alias for this friend, false otherwise.
 */
bool Friend::hasAlias() const
{
    return !userAlias.isEmpty();
}

/**
 * @brief Friend::getToxId
 * @return ToxId of current friend.
 */
const ToxId &Friend::getToxId() const
{
    return userID;
}

/**
 * @brief Friend::getFriendId
 * @return Return friend id.
 */
uint32_t Friend::getFriendId() const
{
    return friendId;
}

/**
 * @brief Friend::setEventFlag
 * @param f True if friend has new event, false otherwise.
 */
void Friend::setEventFlag(bool flag)
{
    hasNewEvents = flag;
}

/**
 * @brief Friend::getEventFlag
 * @return Return true, if friend has new event, false otherwise.
 */
bool Friend::getEventFlag() const
{
    return hasNewEvents;
}

/**
 * @brief Friend::setStatus
 * @param s New status.
 */
void Friend::setStatus(Status s)
{
    if (friendStatus != s)
    {
        friendStatus = s;
        emit statusChanged(friendId, friendStatus);
    }
}

/**
 * @brief Friend::getStatus
 * @return Status of current friend.
 */
Status Friend::getStatus() const
{
    return friendStatus;
}

/**
 * @brief Returns the friend's @a OfflineMessageEngine.
 * @return a const reference to the offline engine
 */
const OfflineMsgEngine& Friend::getOfflineMsgEngine() const
{
    return offlineEngine;
}

void Friend::registerReceipt(int rec, qint64 id, ChatMessage::Ptr msg)
{
    offlineEngine.registerReceipt(rec, id, msg);
}

void Friend::dischargeReceipt(int receipt)
{
    offlineEngine.dischargeReceipt(receipt);
}

void Friend::clearOfflineReceipts()
{
    offlineEngine.removeAllReceipts();
}

void Friend::deliverOfflineMsgs()
{
    offlineEngine.deliverOfflineMsgs();
}
