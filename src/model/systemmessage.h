/*
    Copyright © 2015-2021 by The qTox Project Contributors

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

#include <QObject>
#include <QString>

#include <array>

enum class SystemMessageType
{
    // DO NOT CHANGE ORDER
    // These values are saved directly to the DB and read back, changing the
    // order will break persistence!
    fileSendFailed = 0,
    userJoinedGroup,
    userLeftGroup,
    peerNameChanged,
    peerStateChange,
    titleChanged,
    cleared,
    unexpectedCallEnd,
    outgoingCall,
    incomingCall,
    callEnd,
    messageSendFailed,
};

struct SystemMessage
{
    using Args = std::array<QString, 4>;
    SystemMessageType messageType;
    Args args;

    QString toString() const
    {
        QString translated;
        size_t numArgs = 0;

        switch (messageType) {
        case SystemMessageType::fileSendFailed:
            translated = QObject::tr("Failed to send file \"%1\"");
            numArgs = 1;
            break;
        case SystemMessageType::userJoinedGroup:
            translated = QObject::tr("%1 has joined the group");
            numArgs = 1;
            break;
        case SystemMessageType::userLeftGroup:
            translated = QObject::tr("%1 has left the group");
            numArgs = 1;
            break;
        case SystemMessageType::peerNameChanged:
            translated = QObject::tr("%1 is now known as %2");
            numArgs = 2;
            break;
        case SystemMessageType::titleChanged:
            translated = QObject::tr("%1 has set the title to %2");
            numArgs = 2;
            break;
        case SystemMessageType::cleared:
            translated = QObject::tr("Cleared");
            break;
        case SystemMessageType::unexpectedCallEnd:
            translated = QObject::tr("Call with %1 ended unexpectedly. %2");
            numArgs = 2;
            break;
        case SystemMessageType::callEnd:
            translated = QObject::tr("Call with %1 ended. %2");
            numArgs = 2;
            break;
        case SystemMessageType::peerStateChange:
            translated = QObject::tr("%1 is now %2", "e.g. \"Dubslow is now online\"");
            numArgs = 2;
            break;
        case SystemMessageType::outgoingCall:
            translated = QObject::tr("Calling %1");
            numArgs = 1;
            break;
        case SystemMessageType::incomingCall:
            translated = QObject::tr("%1 calling");
            numArgs = 1;
            break;
        case SystemMessageType::messageSendFailed:
            translated = QObject::tr("Message failed to send");
            break;
        }

        for (size_t i = 0; i < numArgs; ++i) {
            translated = translated.arg(args[i]);
        }

        return translated;
    }
};
