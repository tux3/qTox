/*
    Copyright © 2019 by The qTox Project Contributors

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

#ifndef CHAT_LOG_ITEM_H
#define CHAT_LOG_ITEM_H

#include "src/core/toxfile.h"
#include "src/core/toxpk.h"
#include "src/model/message.h"

#include <memory>

struct ChatLogMessage
{
    bool isComplete;
    Message message;
};

struct ChatLogFile
{
    QDateTime timestamp;
    ToxFile file;
};

class ChatLogItem
{
private:
    using ContentPtr = std::unique_ptr<void, void (*)(void*)>;

public:
    enum class ContentType
    {
        message,
        fileTransfer,
    };

    ChatLogItem(ToxPk sender_, ChatLogFile file_);
    ChatLogItem(ToxPk sender_, ChatLogMessage message_);
    const ToxPk& getSender() const;
    ContentType getContentType() const;
    ChatLogFile& getContentAsFile();
    const ChatLogFile& getContentAsFile() const;
    ChatLogMessage& getContentAsMessage();
    const ChatLogMessage& getContentAsMessage() const;
    QDateTime getTimestamp() const;
    void setDisplayName(QString name);
    const QString& getDisplayName() const;

private:
    ChatLogItem(ToxPk sender_, ContentType contentType_, ContentPtr content_);

    ToxPk sender;
    QString displayName;
    ContentType contentType;

    ContentPtr content;
};

#endif /*CHAT_LOG_ITEM_H*/