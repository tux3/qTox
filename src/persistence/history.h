/*
    Copyright © 2015-2019 by The qTox Project Contributors

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

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QVector>

#include <cassert>
#include <cstdint>
#include <tox/toxencryptsave.h>

#include "src/core/toxfile.h"
#include "src/core/toxpk.h"
#include "src/core/extension.h"
#include "src/model/brokenmessagereason.h"
#include "src/persistence/db/rawdatabase.h"
#include "src/widget/searchtypes.h"

class Profile;
class HistoryKeeper;

enum class HistMessageContentType
{
    message,
    file
};

class HistMessageContent
{
public:
    HistMessageContent(QString message)
        : data(std::make_shared<QString>(std::move(message)))
        , type(HistMessageContentType::message)
    {}

    HistMessageContent(ToxFile file)
        : data(std::make_shared<ToxFile>(std::move(file)))
        , type(HistMessageContentType::file)
    {}

    HistMessageContentType getType() const
    {
        return type;
    }

    QString& asMessage()
    {
        assert(type == HistMessageContentType::message);
        return *static_cast<QString*>(data.get());
    }

    ToxFile& asFile()
    {
        assert(type == HistMessageContentType::file);
        return *static_cast<ToxFile*>(data.get());
    }

    const QString& asMessage() const
    {
        assert(type == HistMessageContentType::message);
        return *static_cast<QString*>(data.get());
    }

    const ToxFile& asFile() const
    {
        assert(type == HistMessageContentType::file);
        return *static_cast<ToxFile*>(data.get());
    }

private:
    // Not really shared but shared_ptr has support for shared_ptr<void>
    std::shared_ptr<void> data;
    HistMessageContentType type;
};

struct FileDbInsertionData
{
    FileDbInsertionData();

    RowId historyId;
    ToxPk friendPk;
    QString fileId;
    QString fileName;
    QString filePath;
    int64_t size;
    int direction;
};
Q_DECLARE_METATYPE(FileDbInsertionData)

enum class MessageState
{
    complete,
    pending,
    broken
};

class History : public QObject, public std::enable_shared_from_this<History>
{
    Q_OBJECT
public:
    struct HistMessage
    {
        HistMessage(RowId id, MessageState state, ExtensionSet extensionSet, QDateTime timestamp, QString chat, QString dispName,
                    QString sender, QString message)
            : chat{chat}
            , sender{sender}
            , dispName{dispName}
            , timestamp{timestamp}
            , id{id}
            , state{state}
            , extensionSet(extensionSet)
            , content(std::move(message))
        {}

        HistMessage(RowId id, MessageState state, QDateTime timestamp, QString chat, QString dispName,
                    QString sender, ToxFile file)
            : chat{chat}
            , sender{sender}
            , dispName{dispName}
            , timestamp{timestamp}
            , id{id}
            , state{state}
            , content(std::move(file))
        {}


        QString chat;
        QString sender;
        QString dispName;
        QDateTime timestamp;
        RowId id;
        MessageState state;
        ExtensionSet extensionSet;
        HistMessageContent content;
    };

    struct DateIdx
    {
        QDate date;
        size_t numMessagesIn;
    };

public:
    explicit History(std::shared_ptr<RawDatabase> db);
    ~History();

    bool isValid();

    bool historyExists(const ToxPk& friendPk);

    void eraseHistory();
    void removeFriendHistory(const ToxPk& friendPk);
    void addNewMessage(const ToxPk& friendPk, const QString& message, const ToxPk& sender,
                       const QDateTime& time, bool isDelivered, ExtensionSet extensions,
                       QString dispName, const std::function<void(RowId)>& insertIdCallback = {});

    void addNewFileMessage(const ToxPk& friendPk, const QString& fileId,
                           const QString& fileName, const QString& filePath, int64_t size,
                           const ToxPk& sender, const QDateTime& time, QString const& dispName);

    void setFileFinished(const QString& fileId, bool success, const QString& filePath, const QByteArray& fileHash);
    size_t getNumMessagesForFriend(const ToxPk& friendPk);
    size_t getNumMessagesForFriendBeforeDate(const ToxPk& friendPk, const QDateTime& date);
    QList<HistMessage> getMessagesForFriend(const ToxPk& friendPk, size_t firstIdx, size_t lastIdx);
    QList<HistMessage> getUndeliveredMessagesForFriend(const ToxPk& friendPk);
    QDateTime getDateWhereFindPhrase(const ToxPk& friendPk, const QDateTime& from, QString phrase,
                                     const ParameterSearch& parameter);
    QList<DateIdx> getNumMessagesForFriendBeforeDateBoundaries(const ToxPk& friendPk,
                                                               const QDate& from, size_t maxNum);

    void markAsDelivered(RowId messageId);
    void markAsBroken(RowId messageId, BrokenMessageReason reason);

signals:
    void fileInserted(RowId dbId, QString fileId);

private slots:
    void onFileInserted(RowId dbId, QString fileId);

private:
    QVector<RawDatabase::Query>
    generateNewFileTransferQueries(const ToxPk& friendPk, const ToxPk& sender, const QDateTime& time,
                                   const QString& dispName, const FileDbInsertionData& insertionData);
    bool historyAccessBlocked();
    static RawDatabase::Query generateFileFinished(RowId fileId, bool success,
                                                   const QString& filePath, const QByteArray& fileHash);

    int64_t getPeerId(ToxPk const& pk);

    std::shared_ptr<RawDatabase> db;


    struct FileInfo
    {
        bool finished = false;
        bool success = false;
        QString filePath;
        QByteArray fileHash;
        RowId fileId{-1};
    };

    // This needs to be a shared pointer to avoid callback lifetime issues
    QHash<QString, FileInfo> fileInfos;
};
