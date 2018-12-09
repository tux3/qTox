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


#include "friend.h"
#include "src/model/group.h"
#include "src/grouplist.h"
#include "src/persistence/profile.h"
#include "src/widget/form/chatform.h"

Friend::Friend(uint32_t friendId, const ToxPk& friendPk, const QString& userAlias, const QString& userName)
    : userName{userName}
    , userAlias{userAlias}
    , friendPk{friendPk}
    , friendId{friendId}
    , hasNewEvents{false}
    , friendStatus{Status::Offline}
{
    if (userName.isEmpty()) {
        this->userName = friendPk.toString();
    }
}

/**
 * @brief Friend::setName sets a new username for the friend
 * @param _name new username, sets the public key if _name is empty
 */
void Friend::setName(const QString& _name)
{
    QString name = _name;
    if (name.isEmpty()) {
        name = friendPk.toString();
    }

    // save old displayed name to be able to compare for changes
    const auto oldDisplayed = getDisplayedName();
    if (userName == userAlias) {
        userAlias.clear(); // Because userAlias was set on name change before (issue #5013)
                           // we clear alias if equal to old name so that name change is visible.
                           // TODO: We should not modify alias on setName.
    }
    if (userName != name) {
        userName = name;
        emit nameChanged(friendId, name);
    }

    const auto newDisplayed = getDisplayedName();
    if (oldDisplayed != newDisplayed) {
        emit displayedNameChanged(newDisplayed);
    }
}
/**
 * @brief Friend::setAlias sets the alias for the friend
 * @param alias new alias, removes it if set to an empty string
 */
void Friend::setAlias(const QString& alias)
{
    if (userAlias == alias) {
        return;
    }
    emit aliasChanged(friendId, alias);

    // save old displayed name to be able to compare for changes
    const auto oldDisplayed = getDisplayedName();
    userAlias = alias;

    const auto newDisplayed = getDisplayedName();
    if (oldDisplayed != newDisplayed) {
        emit displayedNameChanged(newDisplayed);
    }
}

void Friend::setStatusMessage(const QString& message)
{
    if (statusMessage != message) {
        statusMessage = message;
        emit statusMessageChanged(friendId, message);
    }
}

QString Friend::getStatusMessage() const
{
    return statusMessage;
}

/**
 * @brief Friend::getDisplayedName Gets the name that should be displayed for a user
 * @return a QString containing either alias, username or public key
 * @note This function and corresponding signal should be preferred over getting
 *       the name or alias directly.
 */
QString Friend::getDisplayedName() const
{
    if (userAlias.isEmpty()) {
        return userName;
    }

    return userAlias;
}

bool Friend::hasAlias() const
{
    return !userAlias.isEmpty();
}

const ToxPk& Friend::getPublicKey() const
{
    return friendPk;
}

uint32_t Friend::getId() const
{
    return friendId;
}

const ToxPk Friend::getPersistentId() const
{
    return friendPk;
}

void Friend::setEventFlag(bool flag)
{
    hasNewEvents = flag;
}

bool Friend::getEventFlag() const
{
    return hasNewEvents;
}

void Friend::setStatus(Status s)
{
    if (friendStatus != s) {
        friendStatus = s;
        emit statusChanged(friendId, friendStatus);
    }
}

Status Friend::getStatus() const
{
    return friendStatus;
}
