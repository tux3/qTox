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

#include "message.h"
#include "friend.h"
#include "src/core/core.h"

void MessageProcessor::SharedParams::onUserNameSet(const QString& username)
{
    QString sanename = username;
    sanename.remove(QRegExp("[\\t\\n\\v\\f\\r\\x0000]"));
    nameMention = QRegExp("\\b" + QRegExp::escape(username) + "\\b", Qt::CaseInsensitive);
    sanitizedNameMention = QRegExp("\\b" + QRegExp::escape(sanename) + "\\b", Qt::CaseInsensitive);
}

MessageProcessor::MessageProcessor(MessageProcessor::SharedParams* sharedParams)
    : sharedParams(sharedParams)
{}

/**
 * @brief Converts an outgoing message into one (or many) sanitized Message(s)
 */
std::vector<Message> MessageProcessor::processOutgoingMessage(bool isAction, QString const& content)
{
    std::vector<Message> ret;


    QStringList splitMsgs = Core::splitMessage(content, tox_max_message_length());
    ret.reserve(splitMsgs.size());

    QDateTime timestamp = QDateTime::currentDateTime();
    std::transform(splitMsgs.begin(), splitMsgs.end(), std::back_inserter(ret),
                   [&](const QString& part) {
                       Message message;
                       message.isAction = isAction;
                       message.content = part;
                       message.timestamp = timestamp;
                       return message;
                   });

    return ret;
}


/**
 * @brief Converts an incoming message into a sanitized Message
 */
Message MessageProcessor::processIncomingMessage(bool isAction, QString const& message)
{
    QDateTime timestamp = QDateTime::currentDateTime();
    auto ret = Message{};
    ret.isAction = isAction;
    ret.content = message;
    ret.timestamp = timestamp;

    if (detectingMentions && sharedParams) {
        auto core = Core::getInstance();
        auto nameMention = sharedParams->GetNameMention();
        auto sanitizedNameMention = sharedParams->GetSanitizedNameMention();

        for (auto const& mention : {nameMention, sanitizedNameMention}) {
            if (mention.indexIn(ret.content) == -1) {
                continue;
            }

            auto pos = static_cast<size_t>(mention.pos(0));
            auto length = static_cast<size_t>(mention.matchedLength());

            ret.metadata.push_back({MessageMetadataType::selfMention, pos, pos + length});
            break;
        }
    }

    return ret;
}
