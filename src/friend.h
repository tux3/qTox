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

#ifndef FRIEND_H
#define FRIEND_H

#include <QObject>
#include <QString>
#include "src/chatlog/chatmessage.h"
#include "src/core/corestructs.h"
#include "src/core/toxid.h"
#include "src/persistence/offlinemsgengine.h"

class FriendWidget;
class OfflineMsgEngine;

class Friend : public QObject
{
    Q_OBJECT
public:
    typedef uint32_t ID;
    class Private;

    static Friend get(ID friendId);
    static Friend get(const ToxId& userId);
    static QList<Friend> getAll();
    static void remove(ID friendId);

public:
    Friend(ID friendId, const ToxId& userId);
    Friend(Private* data = nullptr);
    Friend(const Friend& other);
    Friend(Friend&& other);
    ~Friend();

    Friend& operator=(const Friend& other);
    Friend& operator=(Friend&& other);
    bool operator==(const Friend& other);
    bool operator!=(const Friend& other);

    bool isValid() const;
    void loadHistory();
    void destroy();

    void setName(QString name);
    void setAlias(QString name);
    QString getDisplayedName() const;
    bool hasAlias() const;

    void setStatusMessage(QString message);
    QString getStatusMessage();

    void setEventFlag(bool f);
    bool getEventFlag() const;

    const ToxId& getToxId() const;
    ID getFriendId() const;

    void setStatus(Status s);
    Status getStatus() const;

    const OfflineMsgEngine& getOfflineMsgEngine() const;
    void registerReceipt(int rec, qint64 id, ChatMessage::Ptr msg);
    void dischargeReceipt(int receipt);

    void clearOfflineReceipts();
    void deliverOfflineMsgs();

signals:
    // TODO: move signals to DB object
    void nameChanged(const QString& name);
    void aliasChanged(ID friendId, QString alias);
    void statusChanged(ID friendId, Status status);
    void newStatusMessage(const QString& message);
    void loadChatHistory();

private:
    QExplicitlySharedDataPointer<Private> data;
    static QHash<ID, Friend::Private*> friendList;
    static QHash<QString, ID> tox2id;
};

#endif // FRIEND_H
