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

#include "chatmessage.h"
#include "chatlinecontentproxy.h"
#include "textformatter.h"
#include "content/filetransferwidget.h"
#include "content/image.h"
#include "content/notificationicon.h"
#include "content/spinner.h"
#include "content/text.h"
#include "content/timestamp.h"
#include "src/widget/style.h"

#include <QDebug>
#include <QCryptographicHash>

#include "src/persistence/settings.h"
#include "src/persistence/smileypack.h"

#define NAME_COL_WIDTH 90.0
#define TIME_COL_WIDTH 90.0


ChatMessage::ChatMessage()
{
}

ChatMessage::Ptr ChatMessage::createChatMessage(const QString& sender, const QString& rawMessage,
                                                MessageType type, bool isMe, const QDateTime& date, bool colorizeName)
{
    ChatMessage::Ptr msg = ChatMessage::Ptr(new ChatMessage);

    QString text = rawMessage.toHtmlEscaped();
    QString senderText = sender;

    auto textType = Text::NORMAL;
    // smileys
    if (Settings::getInstance().getUseEmoticons())
        text = SmileyPack::getInstance().smileyfied(text);

    // quotes (green text)
    text = detectQuotes(text, type);
    text = highlightURI(text);

    // text styling
    Settings::StyleType styleType = Settings::getInstance().getStylePreference();
    if (styleType != Settings::StyleType::NONE) {
        text = applyMarkdown(text, styleType == Settings::StyleType::WITH_CHARS);
    }


    switch (type) {
    case NORMAL:
        text = wrapDiv(text, "msg");
        break;
    case ACTION:
        textType = Text::ACTION;
        senderText = "*";
        text = wrapDiv(QString("%1 %2").arg(sender.toHtmlEscaped(), text), "action");
        msg->setAsAction();
        break;
    case ALERT:
        text = wrapDiv(text, "alert");
        break;
    }

    // Note: Eliding cannot be enabled for RichText items. (QTBUG-17207)
    QFont baseFont = Settings::getInstance().getChatMessageFont();
    QFont authorFont = baseFont;
    if (isMe)
        authorFont.setBold(true);

    QColor color = Style::getColor(Style::MainText);
    if (colorizeName) {
        QByteArray hash = QCryptographicHash::hash((sender.toUtf8()), QCryptographicHash::Sha256);
        quint8 *data = (quint8*)hash.data();

        color.setHsv(data[0], 255, 196);

        if (!isMe && textType == Text::NORMAL) {
                textType = Text::CUSTOM;
        }
    }

    msg->addColumn(new Text(senderText, authorFont, true, sender, textType, color),
                   ColumnFormat(NAME_COL_WIDTH, ColumnFormat::FixedSize, ColumnFormat::Right));
    msg->addColumn(new Text(text, baseFont, false, ((type == ACTION) && isMe)
                                                       ? QString("%1 %2").arg(sender, rawMessage)
                                                       : rawMessage),
                   ColumnFormat(1.0, ColumnFormat::VariableSize));
    msg->addColumn(new Spinner(Style::getImagePath("chatArea/spinner.svg"), QSize(16, 16), 360.0 / 1.6),
                   ColumnFormat(TIME_COL_WIDTH, ColumnFormat::FixedSize, ColumnFormat::Right));

    if (!date.isNull())
        msg->markAsSent(date);

    return msg;
}

ChatMessage::Ptr ChatMessage::createChatInfoMessage(const QString& rawMessage,
                                                    SystemMessageType type, const QDateTime& date)
{
    ChatMessage::Ptr msg = ChatMessage::Ptr(new ChatMessage);
    QString text = rawMessage.toHtmlEscaped();

    QString img;
    switch (type) {
    case INFO:
        img = Style::getImagePath("chatArea/info.svg");
        break;
    case ERROR:
        img = Style::getImagePath("chatArea/error.svg");
        break;
    case TYPING:
        img = Style::getImagePath("chatArea/typing.svg");
        break;
    }

    QFont baseFont = Settings::getInstance().getChatMessageFont();

    msg->addColumn(new Image(QSize(18, 18), img),
                   ColumnFormat(NAME_COL_WIDTH, ColumnFormat::FixedSize, ColumnFormat::Right));
    msg->addColumn(new Text("<b>" + text + "</b>", baseFont, false, text),
                   ColumnFormat(1.0, ColumnFormat::VariableSize, ColumnFormat::Left));
    msg->addColumn(new Timestamp(date, Settings::getInstance().getTimestampFormat(), baseFont),
                   ColumnFormat(TIME_COL_WIDTH, ColumnFormat::FixedSize, ColumnFormat::Right));

    return msg;
}

ChatMessage::Ptr ChatMessage::createFileTransferMessage(const QString& sender, ToxFile file,
                                                        bool isMe, const QDateTime& date)
{
    ChatMessage::Ptr msg = ChatMessage::Ptr(new ChatMessage);

    QFont baseFont = Settings::getInstance().getChatMessageFont();
    QFont authorFont = baseFont;
    if (isMe)
        authorFont.setBold(true);

    msg->addColumn(new Text(sender, authorFont, true),
                   ColumnFormat(NAME_COL_WIDTH, ColumnFormat::FixedSize, ColumnFormat::Right));
    msg->addColumn(new ChatLineContentProxy(new FileTransferWidget(nullptr, file), 320, 0.6f),
                   ColumnFormat(1.0, ColumnFormat::VariableSize));
    msg->addColumn(new Timestamp(date, Settings::getInstance().getTimestampFormat(), baseFont),
                   ColumnFormat(TIME_COL_WIDTH, ColumnFormat::FixedSize, ColumnFormat::Right));

    return msg;
}

ChatMessage::Ptr ChatMessage::createTypingNotification()
{
    ChatMessage::Ptr msg = ChatMessage::Ptr(new ChatMessage);

    QFont baseFont = Settings::getInstance().getChatMessageFont();

    // Note: "[user]..." is just a placeholder. The actual text is set in
    // ChatForm::setFriendTyping()
    //
    // FIXME: Due to circumstances, placeholder is being used in a case where
    // user received typing notifications constantly since contact came online.
    // This causes "[user]..." to be displayed in place of user nick, as long
    // as user will keep typing. Issue #1280
    msg->addColumn(new NotificationIcon(QSize(18, 18)),
                   ColumnFormat(NAME_COL_WIDTH, ColumnFormat::FixedSize, ColumnFormat::Right));
    msg->addColumn(new Text("[user]...", baseFont, false, ""),
                   ColumnFormat(1.0, ColumnFormat::VariableSize, ColumnFormat::Left));

    return msg;
}

/**
 * @brief Create message placeholder while chatform restructures text
 *
 * It can take a while for chatform to resize large amounts of text, thus
 * a message placeholder is needed to inform users about it.
 *
 * @return created message
 */
ChatMessage::Ptr ChatMessage::createBusyNotification()
{
    ChatMessage::Ptr msg = ChatMessage::Ptr(new ChatMessage);
    QFont baseFont = Settings::getInstance().getChatMessageFont();
    baseFont.setPixelSize(baseFont.pixelSize() + 2);
    baseFont.setBold(true);

    msg->addColumn(new Text(QObject::tr("Reformatting text in progress.."), baseFont, false, ""),
                   ColumnFormat(1.0, ColumnFormat::VariableSize, ColumnFormat::Center));

    return msg;
}

void ChatMessage::markAsSent(const QDateTime& time)
{
    QFont baseFont = Settings::getInstance().getChatMessageFont();

    // remove the spinner and replace it by $time
    replaceContent(2, new Timestamp(time, Settings::getInstance().getTimestampFormat(), baseFont));
}

QString ChatMessage::toString() const
{
    ChatLineContent* c = getContent(1);
    if (c)
        return c->getText();

    return QString();
}

bool ChatMessage::isAction() const
{
    return action;
}

void ChatMessage::setAsAction()
{
    action = true;
}

void ChatMessage::hideSender()
{
    ChatLineContent* c = getContent(0);
    if (c)
        c->hide();
}

void ChatMessage::hideDate()
{
    ChatLineContent* c = getContent(2);
    if (c)
        c->hide();
}

QString ChatMessage::detectQuotes(const QString& str, MessageType type)
{
    // detect text quotes
    QStringList messageLines = str.split("\n");
    QString quotedText;
    for (int i = 0; i < messageLines.size(); ++i) {
        // don't quote first line in action message. This makes co-existence of
        // quotes and action messages possible, since only first line can cause
        // problems in case where there is quote in it used.
        if (QRegExp("^(&gt;|＞).*").exactMatch(messageLines[i])) {
            if (i > 0 || type != ACTION)
                quotedText += "<span class=quote>" + messageLines[i] + " </span>";
            else
                quotedText += messageLines[i];
        } else {
            quotedText += messageLines[i];
        }

        if (i < messageLines.size() - 1) {
            quotedText += '\n';
        }
    }

    return quotedText;
}

QString ChatMessage::wrapDiv(const QString& str, const QString& div)
{
    return QString("<p class=%1>%2</p>").arg(div, /*QChar(0x200E) + */ QString(str));
}
