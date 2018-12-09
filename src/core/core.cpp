/*
    Copyright (C) 2013 by Maxim Biro <nurupo.contributions@gmail.com>
    Copyright © 2014-2018 by The qTox Project Contributors

    This file is part of qTox, a Qt-based graphical interface for Tox.

    This program is free software: you can redistribute it and/or modify
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

#include "core.h"
#include "corefile.h"
#include "src/core/coreav.h"
#include "src/core/icoresettings.h"
#include "src/core/toxlogger.h"
#include "src/core/toxoptions.h"
#include "src/core/toxstring.h"
#include "src/model/groupinvite.h"
#include "src/nexus.h"
#include "src/persistence/profile.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QString>
#include <QStringBuilder>
#include <QTimer>

#include <cassert>
#include <memory>

const QString Core::TOX_EXT = ".tox";

#define MAX_GROUP_MESSAGE_LEN 1024

#define ASSERT_CORE_THREAD assert(QThread::currentThread() == coreThread.get())

namespace {
    bool LogConferenceTitleError(TOX_ERR_CONFERENCE_TITLE error)
    {
        switch(error)
        {
        case TOX_ERR_CONFERENCE_TITLE_OK:
            break;
        case TOX_ERR_CONFERENCE_TITLE_CONFERENCE_NOT_FOUND:
            qWarning() << "Conference title not found";
            break;
        case TOX_ERR_CONFERENCE_TITLE_INVALID_LENGTH:
            qWarning() << "Invalid conference title length";
            break;
        case TOX_ERR_CONFERENCE_TITLE_FAIL_SEND:
            qWarning() << "Failed to send title packet";
        }
        return error;
    }
} // namespace

Core::Core(QThread* coreThread)
    : tox(nullptr)
    , av(nullptr)
    , toxTimer{new QTimer{this}}
    , coreLoopLock(new QMutex(QMutex::Recursive))
    , coreThread(coreThread)
{
    assert(toxTimer);
    toxTimer->setSingleShot(true);
    connect(toxTimer, &QTimer::timeout, this, &Core::process);
    connect(coreThread, &QThread::finished, toxTimer, &QTimer::stop);
}

Core::~Core()
{
    // need to reset av first, because it uses tox
    av.reset();

    coreThread->exit(0);
    coreThread->wait();

    tox.reset();
}

/**
 * @brief Registers all toxcore callbacks
 * @param tox Tox instance to register the callbacks on
 */
void Core::registerCallbacks(Tox* tox)
{
    tox_callback_friend_request(tox, onFriendRequest);
    tox_callback_friend_message(tox, onFriendMessage);
    tox_callback_friend_name(tox, onFriendNameChange);
    tox_callback_friend_typing(tox, onFriendTypingChange);
    tox_callback_friend_status_message(tox, onStatusMessageChanged);
    tox_callback_friend_status(tox, onUserStatusChanged);
    tox_callback_friend_connection_status(tox, onConnectionStatusChanged);
    tox_callback_friend_read_receipt(tox, onReadReceiptCallback);
    tox_callback_conference_invite(tox, onGroupInvite);
    tox_callback_conference_message(tox, onGroupMessage);
    tox_callback_conference_peer_list_changed(tox, onGroupPeerListChange);
    tox_callback_conference_peer_name(tox, onGroupPeerNameChange);
    tox_callback_conference_title(tox, onGroupTitleChange);
    tox_callback_file_chunk_request(tox, CoreFile::onFileDataCallback);
    tox_callback_file_recv(tox, CoreFile::onFileReceiveCallback);
    tox_callback_file_recv_chunk(tox, CoreFile::onFileRecvChunkCallback);
    tox_callback_file_recv_control(tox, CoreFile::onFileControlCallback);
}

/**
 * @brief Factory method for the Core object
 * @param savedata empty if new profile or saved data else
 * @param settings Settings specific to Core
 * @return nullptr or a Core object ready to start
 */
ToxCorePtr Core::makeToxCore(const QByteArray& savedata, const ICoreSettings* const settings,
                             ToxCoreErrors* err)
{
    QThread* thread = new QThread();
    if (thread == nullptr) {
        qCritical() << "could not allocate Core thread";
        return {};
    }
    thread->setObjectName("qTox Core");

    auto toxOptions = ToxOptions::makeToxOptions(savedata, settings);
    if (toxOptions == nullptr) {
        qCritical() << "could not allocate Tox Options data structure";
        if (err) {
            *err = ToxCoreErrors::ERROR_ALLOC;
        }
        return {};
    }

    ToxCorePtr core(new Core(thread));
    if (core == nullptr) {
        if (err) {
            *err = ToxCoreErrors::ERROR_ALLOC;
        }
        return {};
    }

    Tox_Err_New tox_err;
    core->tox = ToxPtr(tox_new(*toxOptions, &tox_err));

    switch (tox_err) {
    case TOX_ERR_NEW_OK:
        break;

    case TOX_ERR_NEW_LOAD_BAD_FORMAT:
        qCritical() << "failed to parse Tox save data";
        if (err) {
            *err = ToxCoreErrors::BAD_PROXY;
        }
        return {};

    case TOX_ERR_NEW_PORT_ALLOC:
        if (toxOptions->getIPv6Enabled()) {
            toxOptions->setIPv6Enabled(false);
            core->tox = ToxPtr(tox_new(*toxOptions, &tox_err));
            if (tox_err == TOX_ERR_NEW_OK) {
                qWarning() << "Core failed to start with IPv6, falling back to IPv4. LAN discovery "
                              "may not work properly.";
                break;
            }
        }

        qCritical() << "can't to bind the port";
        if (err) {
            *err = ToxCoreErrors::FAILED_TO_START;
        }
        return {};

    case TOX_ERR_NEW_PROXY_BAD_HOST:
    case TOX_ERR_NEW_PROXY_BAD_PORT:
    case TOX_ERR_NEW_PROXY_BAD_TYPE:
        qCritical() << "bad proxy, error code:" << tox_err;
        if (err) {
            *err = ToxCoreErrors::BAD_PROXY;
        }
        return {};

    case TOX_ERR_NEW_PROXY_NOT_FOUND:
        qCritical() << "proxy not found";
        if (err) {
            *err = ToxCoreErrors::BAD_PROXY;
        }
        return {};

    case TOX_ERR_NEW_LOAD_ENCRYPTED:
        qCritical() << "attempted to load encrypted Tox save data";
        if (err) {
            *err = ToxCoreErrors::INVALID_SAVE;
        }
        return {};

    case TOX_ERR_NEW_MALLOC:
        qCritical() << "memory allocation failed";
        if (err) {
            *err = ToxCoreErrors::ERROR_ALLOC;
        }
        return {};

    case TOX_ERR_NEW_NULL:
        qCritical() << "a parameter was null";
        if (err) {
            *err = ToxCoreErrors::FAILED_TO_START;
        }
        return {};

    default:
        qCritical() << "Tox core failed to start, unknown error code:" << tox_err;
        if (err) {
            *err = ToxCoreErrors::FAILED_TO_START;
        }
        return {};
    }

    // provide a list of bootstrap nodes
    core->bootstrapNodes = settings->getDhtServerList();

    // tox should be valid by now
    assert(core->tox != nullptr);

    // toxcore is successfully created, create toxav
    core->av = CoreAV::makeCoreAV(core->tox.get());
    if (!core->av) {
        qCritical() << "Toxav failed to start";
        if (err) {
            *err = ToxCoreErrors::FAILED_TO_START;
        }
        return {};
    }

    registerCallbacks(core->tox.get());

    // connect the thread with the Core
    connect(thread, &QThread::started, core.get(), &Core::onStarted);
    core->moveToThread(thread);

    // when leaving this function 'core' should be ready for it's start() action or
    // a nullptr
    return core;
}

void Core::onStarted()
{
    ASSERT_CORE_THREAD;

    // One time initialization stuff
    QString name = getUsername();
    if (!name.isEmpty()) {
        emit usernameSet(name);
    }

    QString msg = getStatusMessage();
    if (!msg.isEmpty()) {
        emit statusMessageSet(msg);
    }

    ToxId id = getSelfId();
    // Id comes from toxcore, must be valid
    assert(id.isValid());
    emit idSet(id);

    loadFriends();
    loadGroups();

    process(); // starts its own timer
    av->start();
    emit avReady();
}

/**
 * @brief Starts toxcore and it's event loop, can be called from any thread
 */
void Core::start()
{
    coreThread->start();
}


/**
 * @brief Returns the global widget's Core instance
 */
Core* Core::getInstance()
{
    return Nexus::getCore();
}

const CoreAV* Core::getAv() const
{
    return av.get();
}

CoreAV* Core::getAv()
{
    return av.get();
}

/* Using the now commented out statements in checkConnection(), I watched how
 * many ticks disconnects-after-initial-connect lasted. Out of roughly 15 trials,
 * 5 disconnected; 4 were DCd for less than 20 ticks, while the 5th was ~50 ticks.
 * So I set the tolerance here at 25, and initial DCs should be very rare now.
 * This should be able to go to 50 or 100 without affecting legitimate disconnects'
 * downtime, but lets be conservative for now. Edit: now ~~40~~ 30.
 */
#define CORE_DISCONNECT_TOLERANCE 30

/**
 * @brief Processes toxcore events and ensure we stay connected, called by its own timer
 */
void Core::process()
{
    QMutexLocker ml{coreLoopLock.get()};

    ASSERT_CORE_THREAD;

    static int tolerance = CORE_DISCONNECT_TOLERANCE;
    tox_iterate(tox.get(), this);

#ifdef DEBUG
    // we want to see the debug messages immediately
    fflush(stdout);
#endif

    // TODO(sudden6): recheck if this is still necessary
    if (checkConnection()) {
        tolerance = CORE_DISCONNECT_TOLERANCE;
    } else if (!(--tolerance)) {
        bootstrapDht();
        tolerance = 3 * CORE_DISCONNECT_TOLERANCE;
    }

    unsigned sleeptime =
        qMin(tox_iteration_interval(tox.get()), CoreFile::corefileIterationInterval());
    toxTimer->start(sleeptime);
}

bool Core::checkConnection()
{
    ASSERT_CORE_THREAD;
    static bool isConnected = false;
    bool toxConnected = tox_self_get_connection_status(tox.get()) != TOX_CONNECTION_NONE;
    if (toxConnected && !isConnected) {
        qDebug() << "Connected to the DHT";
        emit connected();
    } else if (!toxConnected && isConnected) {
        qDebug() << "Disconnected from the DHT";
        emit disconnected();
    }

    isConnected = toxConnected;
    return toxConnected;
}

/**
 * @brief Connects us to the Tox network
 */
void Core::bootstrapDht()
{
    ASSERT_CORE_THREAD;
    int listSize = bootstrapNodes.size();
    if (!listSize) {
        qWarning() << "no bootstrap list?!?";
        return;
    }

    int i = 0;
    static int j = qrand() % listSize;
    // i think the more we bootstrap, the more we jitter because the more we overwrite nodes
    while (i < 2) {
        const DhtServer& dhtServer = bootstrapNodes[j % listSize];
        QString dhtServerAddress = dhtServer.address.toLatin1();
        QString port = QString::number(dhtServer.port);
        QString name = dhtServer.name;
        qDebug() << QString("Connecting to %1:%2 (%3)").arg(dhtServerAddress, port, name);
        QByteArray address = dhtServer.address.toLatin1();
        // TODO: constucting the pk via ToxId is a workaround
        ToxPk pk = ToxId{dhtServer.userId}.getPublicKey();


        const uint8_t* pkPtr = reinterpret_cast<const uint8_t*>(pk.getBytes());

        if (!tox_bootstrap(tox.get(), address.constData(), dhtServer.port, pkPtr, nullptr)) {
            qDebug() << "Error bootstrapping from " + dhtServer.name;
        }

        if (!tox_add_tcp_relay(tox.get(), address.constData(), dhtServer.port, pkPtr, nullptr)) {
            qDebug() << "Error adding TCP relay from " + dhtServer.name;
        }

        ++j;
        ++i;
    }
}

void Core::onFriendRequest(Tox*, const uint8_t* cFriendPk, const uint8_t* cMessage,
                           size_t cMessageSize, void* core)
{
    ToxPk friendPk(cFriendPk);
    QString requestMessage = ToxString(cMessage, cMessageSize).getQString();
    emit static_cast<Core*>(core)->friendRequestReceived(friendPk, requestMessage);
}

void Core::onFriendMessage(Tox*, uint32_t friendId, Tox_Message_Type type, const uint8_t* cMessage,
                           size_t cMessageSize, void* core)
{
    bool isAction = (type == TOX_MESSAGE_TYPE_ACTION);
    QString msg = ToxString(cMessage, cMessageSize).getQString();
    emit static_cast<Core*>(core)->friendMessageReceived(friendId, msg, isAction);
}

void Core::onFriendNameChange(Tox*, uint32_t friendId, const uint8_t* cName, size_t cNameSize, void* core)
{
    QString newName = ToxString(cName, cNameSize).getQString();
    emit static_cast<Core*>(core)->friendUsernameChanged(friendId, newName);
}

void Core::onFriendTypingChange(Tox*, uint32_t friendId, bool isTyping, void* core)
{
    emit static_cast<Core*>(core)->friendTypingChanged(friendId, isTyping);
}

void Core::onStatusMessageChanged(Tox*, uint32_t friendId, const uint8_t* cMessage,
                                  size_t cMessageSize, void* core)
{
    QString message = ToxString(cMessage, cMessageSize).getQString();
    emit static_cast<Core*>(core)->friendStatusMessageChanged(friendId, message);
}

void Core::onUserStatusChanged(Tox*, uint32_t friendId, Tox_User_Status userstatus, void* core)
{
    Status status;
    switch (userstatus) {
    case TOX_USER_STATUS_AWAY:
        status = Status::Away;
        break;

    case TOX_USER_STATUS_BUSY:
        status = Status::Busy;
        break;

    default:
        status = Status::Online;
        break;
    }

    emit static_cast<Core*>(core)->friendStatusChanged(friendId, status);
}

void Core::onConnectionStatusChanged(Tox*, uint32_t friendId, Tox_Connection status, void* core)
{
    Status friendStatus = status != TOX_CONNECTION_NONE ? Status::Online : Status::Offline;
    // Ignore Online because it will be emited from onUserStatusChanged
    bool isOffline = friendStatus == Status::Offline;
    if (isOffline) {
        emit static_cast<Core*>(core)->friendStatusChanged(friendId, friendStatus);
        static_cast<Core*>(core)->checkLastOnline(friendId);
        CoreFile::onConnectionStatusChanged(static_cast<Core*>(core), friendId, !isOffline);
    }
}

void Core::onGroupInvite(Tox* tox, uint32_t friendId, Tox_Conference_Type type,
                         const uint8_t* cookie, size_t length, void* vCore)
{
    Core* core = static_cast<Core*>(vCore);
    const QByteArray data(reinterpret_cast<const char*>(cookie), length);
    const GroupInvite inviteInfo(friendId, type, data);
    switch (type) {
    case TOX_CONFERENCE_TYPE_TEXT:
        qDebug() << QString("Text group invite by %1").arg(friendId);
        emit core->groupInviteReceived(inviteInfo);
        break;

    case TOX_CONFERENCE_TYPE_AV:
        qDebug() << QString("AV group invite by %1").arg(friendId);
        emit core->groupInviteReceived(inviteInfo);
        break;

    default:
        qWarning() << "Group invite with unknown type " << type;
    }
}

void Core::onGroupMessage(Tox*, uint32_t groupId, uint32_t peerId, Tox_Message_Type type,
                          const uint8_t* cMessage, size_t length, void* vCore)
{
    Core* core = static_cast<Core*>(vCore);
    bool isAction = type == TOX_MESSAGE_TYPE_ACTION;
    QString message = ToxString(cMessage, length).getQString();
    emit core->groupMessageReceived(groupId, peerId, message, isAction);
}

void Core::onGroupPeerListChange(Tox*, uint32_t groupId, void* vCore)
{
    const auto core = static_cast<Core*>(vCore);
    if (core->getGroupAvEnabled(groupId)) {
        CoreAV::invalidateGroupCallSources(groupId);
    }

    qDebug() << QString("Group %1 peerlist changed").arg(groupId);
    emit core->groupPeerlistChanged(groupId);
}

void Core::onGroupPeerNameChange(Tox*, uint32_t groupId, uint32_t peerId, const uint8_t* name,
                                 size_t length, void* core)
{
    const auto newName = ToxString(name, length).getQString();
    qDebug() << QString("Group %1, Peer %2, name changed to %3").arg(groupId).arg(peerId).arg(newName);
    emit static_cast<Core*>(core)->groupPeerNameChanged(groupId, peerId, newName);
}

void Core::onGroupTitleChange(Tox*, uint32_t groupId, uint32_t peerId, const uint8_t* cTitle,
                              size_t length, void* vCore)
{
    Core* core = static_cast<Core*>(vCore);
    QString author = core->getGroupPeerName(groupId, peerId);
    emit core->groupTitleChanged(groupId, author, ToxString(cTitle, length).getQString());
}

void Core::onReadReceiptCallback(Tox*, uint32_t friendId, uint32_t receipt, void* core)
{
    emit static_cast<Core*>(core)->receiptRecieved(friendId, receipt);
}

void Core::acceptFriendRequest(const ToxPk& friendPk)
{
    QMutexLocker ml{coreLoopLock.get()};
    // TODO: error handling
    uint32_t friendId = tox_friend_add_norequest(tox.get(), friendPk.getBytes(), nullptr);
    if (friendId == std::numeric_limits<uint32_t>::max()) {
        emit failedToAddFriend(friendPk);
    } else {
        emit saveRequest();
        emit friendAdded(friendId, friendPk);
    }
}

/**
 * @brief Checks that sending friendship request is correct and returns error message accordingly
 * @param friendId Id of a friend which request is destined to
 * @param message Friendship request message
 * @return Returns empty string if sending request is correct, according error message otherwise
 */
QString Core::getFriendRequestErrorMessage(const ToxId& friendId, const QString& message) const
{
    QMutexLocker ml{coreLoopLock.get()};

    if (!friendId.isValid()) {
        return tr("Invalid Tox ID", "Error while sending friendship request");
    }

    if (message.isEmpty()) {
        return tr("You need to write a message with your request",
                  "Error while sending friendship request");
    }

    if (message.length() > static_cast<int>(tox_max_friend_request_length())) {
        return tr("Your message is too long!", "Error while sending friendship request");
    }

    if (hasFriendWithPublicKey(friendId.getPublicKey())) {
        return tr("Friend is already added", "Error while sending friendship request");
    }

    return QString{};
}

void Core::requestFriendship(const ToxId& friendId, const QString& message)
{
    QMutexLocker ml{coreLoopLock.get()};

    ToxPk friendPk = friendId.getPublicKey();
    QString errorMessage = getFriendRequestErrorMessage(friendId, message);
    if (!errorMessage.isNull()) {
        emit failedToAddFriend(friendPk, errorMessage);
        emit saveRequest();
        return;
    }

    ToxString cMessage(message);
    uint32_t friendNumber =
        tox_friend_add(tox.get(), friendId.getBytes(), cMessage.data(), cMessage.size(), nullptr);
    if (friendNumber == std::numeric_limits<uint32_t>::max()) {
        qDebug() << "Failed to request friendship";
        emit failedToAddFriend(friendPk);
    } else {
        qDebug() << "Requested friendship of " << friendNumber;
        emit friendAdded(friendNumber, friendPk);
        emit requestSent(friendPk, message);
    }

    emit saveRequest();
}

int Core::sendMessage(uint32_t friendId, const QString& message)
{
    QMutexLocker ml(coreLoopLock.get());
    ToxString cMessage(message);
    int receipt = tox_friend_send_message(tox.get(), friendId, TOX_MESSAGE_TYPE_NORMAL,
                                          cMessage.data(), cMessage.size(), nullptr);
    emit messageSentResult(friendId, message, receipt);
    return receipt;
}

int Core::sendAction(uint32_t friendId, const QString& action)
{
    QMutexLocker ml(coreLoopLock.get());
    ToxString cMessage(action);
    int receipt = tox_friend_send_message(tox.get(), friendId, TOX_MESSAGE_TYPE_ACTION,
                                          cMessage.data(), cMessage.size(), nullptr);
    emit messageSentResult(friendId, action, receipt);
    return receipt;
}

void Core::sendTyping(uint32_t friendId, bool typing)
{
    QMutexLocker ml{coreLoopLock.get()};

    if (!tox_self_set_typing(tox.get(), friendId, typing, nullptr)) {
        emit failedToSetTyping(typing);
    }
}

bool parseConferenceSendMessageError(Tox_Err_Conference_Send_Message  error)
{
    switch (error) {
    case TOX_ERR_CONFERENCE_SEND_MESSAGE_OK:
        return true;

    case TOX_ERR_CONFERENCE_SEND_MESSAGE_CONFERENCE_NOT_FOUND:
        qCritical() << "Conference not found";
        return false;

    case TOX_ERR_CONFERENCE_SEND_MESSAGE_FAIL_SEND:
        qCritical() << "Conference message failed to send";
        return false;

    case TOX_ERR_CONFERENCE_SEND_MESSAGE_NO_CONNECTION:
        qCritical() << "No connection";
        return false;

    case TOX_ERR_CONFERENCE_SEND_MESSAGE_TOO_LONG:
        qCritical() << "Message too long";
        return false;

    default:
        qCritical() << "Unknown Tox_Err_Conference_Send_Message  error";
        return false;
    }
}

void Core::sendGroupMessageWithType(int groupId, const QString& message, Tox_Message_Type type)
{
    QMutexLocker ml{coreLoopLock.get()};

    QStringList cMessages = splitMessage(message, MAX_GROUP_MESSAGE_LEN);

    for (auto& part : cMessages) {
        ToxString cMsg(part);
        Tox_Err_Conference_Send_Message  error;
        bool ok =
            tox_conference_send_message(tox.get(), groupId, type, cMsg.data(), cMsg.size(), &error);
        if (!ok || !parseConferenceSendMessageError(error)) {
            emit groupSentFailed(groupId);
            return;
        }
    }
}

void Core::sendGroupMessage(int groupId, const QString& message)
{
    QMutexLocker ml{coreLoopLock.get()};

    sendGroupMessageWithType(groupId, message, TOX_MESSAGE_TYPE_NORMAL);
}

void Core::sendGroupAction(int groupId, const QString& message)
{
    QMutexLocker ml{coreLoopLock.get()};

    sendGroupMessageWithType(groupId, message, TOX_MESSAGE_TYPE_ACTION);
}

void Core::changeGroupTitle(int groupId, const QString& title)
{
    QMutexLocker ml{coreLoopLock.get()};

    ToxString cTitle(title);
    Tox_Err_Conference_Title error;
    bool success = tox_conference_set_title(tox.get(), groupId, cTitle.data(), cTitle.size(), &error);
    if (success && error == TOX_ERR_CONFERENCE_TITLE_OK) {
        emit groupTitleChanged(groupId, getUsername(), title);
        return;
    }

    qCritical() << "Fail of tox_conference_set_title";
    switch (error) {
    case TOX_ERR_CONFERENCE_TITLE_CONFERENCE_NOT_FOUND:
        qCritical() << "Conference not found";
        break;

    case TOX_ERR_CONFERENCE_TITLE_FAIL_SEND:
        qCritical() << "Conference title failed to send";
        break;

    case TOX_ERR_CONFERENCE_TITLE_INVALID_LENGTH:
        qCritical() << "Invalid length";
        break;

    default:
        break;
    }
}

void Core::sendFile(uint32_t friendId, QString filename, QString filePath, long long filesize)
{
    QMutexLocker ml{coreLoopLock.get()};

    CoreFile::sendFile(this, friendId, filename, filePath, filesize);
}

void Core::sendAvatarFile(uint32_t friendId, const QByteArray& data)
{
    QMutexLocker ml{coreLoopLock.get()};

    CoreFile::sendAvatarFile(this, friendId, data);
}

void Core::pauseResumeFileSend(uint32_t friendId, uint32_t fileNum)
{
    QMutexLocker ml{coreLoopLock.get()};

    CoreFile::pauseResumeFileSend(this, friendId, fileNum);
}

void Core::pauseResumeFileRecv(uint32_t friendId, uint32_t fileNum)
{
    QMutexLocker ml{coreLoopLock.get()};

    CoreFile::pauseResumeFileRecv(this, friendId, fileNum);
}

void Core::cancelFileSend(uint32_t friendId, uint32_t fileNum)
{
    QMutexLocker ml{coreLoopLock.get()};

    CoreFile::cancelFileSend(this, friendId, fileNum);
}

void Core::cancelFileRecv(uint32_t friendId, uint32_t fileNum)
{
    QMutexLocker ml{coreLoopLock.get()};

    CoreFile::cancelFileRecv(this, friendId, fileNum);
}

void Core::rejectFileRecvRequest(uint32_t friendId, uint32_t fileNum)
{
    QMutexLocker ml{coreLoopLock.get()};

    CoreFile::rejectFileRecvRequest(this, friendId, fileNum);
}

void Core::acceptFileRecvRequest(uint32_t friendId, uint32_t fileNum, QString path)
{
    QMutexLocker ml{coreLoopLock.get()};

    CoreFile::acceptFileRecvRequest(this, friendId, fileNum, path);
}

void Core::removeFriend(uint32_t friendId, bool fake)
{
    QMutexLocker ml{coreLoopLock.get()};

    if (!isReady() || fake) {
        return;
    }

    if (!tox_friend_delete(tox.get(), friendId, nullptr)) {
        emit failedToRemoveFriend(friendId);
        return;
    }

    emit saveRequest();
    emit friendRemoved(friendId);
}

void Core::removeGroup(int groupId, bool fake)
{
    QMutexLocker ml{coreLoopLock.get()};

    if (!isReady() || fake) {
        return;
    }

    Tox_Err_Conference_Delete error;
    bool success = tox_conference_delete(tox.get(), groupId, &error);
    if (success && error == TOX_ERR_CONFERENCE_DELETE_OK) {
        av->leaveGroupCall(groupId);
        return;
    }

    qCritical() << "Fail of tox_conference_delete";
    switch (error) {
    case TOX_ERR_CONFERENCE_DELETE_CONFERENCE_NOT_FOUND:
        qCritical() << "Conference not found";
        break;

    default:
        break;
    }
}

/**
 * @brief Returns our username, or an empty string on failure
 */
QString Core::getUsername() const
{
    QMutexLocker ml{coreLoopLock.get()};

    QString sname;
    if (!tox) {
        return sname;
    }

    int size = tox_self_get_name_size(tox.get());
    uint8_t* name = new uint8_t[size];
    tox_self_get_name(tox.get(), name);
    sname = ToxString(name, size).getQString();
    delete[] name;
    return sname;
}

void Core::setUsername(const QString& username)
{
    QMutexLocker ml{coreLoopLock.get()};

    if (username == getUsername()) {
        return;
    }

    ToxString cUsername(username);
    if (!tox_self_set_name(tox.get(), cUsername.data(), cUsername.size(), nullptr)) {
        emit failedToSetUsername(username);
        return;
    }

    emit usernameSet(username);
    emit saveRequest();
}

/**
 * @brief Returns our Tox ID
 */
ToxId Core::getSelfId() const
{
    QMutexLocker ml{coreLoopLock.get()};

    uint8_t friendId[TOX_ADDRESS_SIZE] = {0x00};
    tox_self_get_address(tox.get(), friendId);
    return ToxId(friendId, TOX_ADDRESS_SIZE);
}

/**
 * @brief Gets self public key
 * @return Self PK
 */
ToxPk Core::getSelfPublicKey() const
{
    QMutexLocker ml{coreLoopLock.get()};

    uint8_t friendId[TOX_ADDRESS_SIZE] = {0x00};
    tox_self_get_address(tox.get(), friendId);
    return ToxPk(friendId);
}

/**
 * @brief Returns our public and private keys
 */
QPair<QByteArray, QByteArray> Core::getKeypair() const
{
    QMutexLocker ml{coreLoopLock.get()};

    QPair<QByteArray, QByteArray> keypair;
    if (!tox) {
        return keypair;
    }

    QByteArray pk(TOX_PUBLIC_KEY_SIZE, 0x00);
    QByteArray sk(TOX_SECRET_KEY_SIZE, 0x00);
    tox_self_get_public_key(tox.get(), reinterpret_cast<uint8_t*>(pk.data()));
    tox_self_get_secret_key(tox.get(), reinterpret_cast<uint8_t*>(sk.data()));
    keypair.first = pk;
    keypair.second = sk;
    return keypair;
}

/**
 * @brief Returns our status message, or an empty string on failure
 */
QString Core::getStatusMessage() const
{
    QMutexLocker ml{coreLoopLock.get()};

    QString sname;
    if (!tox) {
        return sname;
    }

    size_t size = tox_self_get_status_message_size(tox.get());
    uint8_t* name = new uint8_t[size];
    tox_self_get_status_message(tox.get(), name);
    sname = ToxString(name, size).getQString();
    delete[] name;
    return sname;
}

/**
 * @brief Returns our user status
 */
Status Core::getStatus() const
{
    QMutexLocker ml{coreLoopLock.get()};

    return static_cast<Status>(tox_self_get_status(tox.get()));
}

void Core::setStatusMessage(const QString& message)
{
    QMutexLocker ml{coreLoopLock.get()};

    if (message == getStatusMessage()) {
        return;
    }

    ToxString cMessage(message);
    if (!tox_self_set_status_message(tox.get(), cMessage.data(), cMessage.size(), nullptr)) {
        emit failedToSetStatusMessage(message);
        return;
    }

    emit saveRequest();
    emit statusMessageSet(message);
}

void Core::setStatus(Status status)
{
    QMutexLocker ml{coreLoopLock.get()};

    Tox_User_Status userstatus;
    switch (status) {
    case Status::Online:
        userstatus = TOX_USER_STATUS_NONE;
        break;

    case Status::Away:
        userstatus = TOX_USER_STATUS_AWAY;
        break;

    case Status::Busy:
        userstatus = TOX_USER_STATUS_BUSY;
        break;

    default:
        return;
        break;
    }

    tox_self_set_status(tox.get(), userstatus);
    emit saveRequest();
    emit statusSet(status);
}

/**
 * @brief Returns the unencrypted tox save data
 */
QByteArray Core::getToxSaveData()
{
    QMutexLocker ml{coreLoopLock.get()};

    uint32_t fileSize = tox_get_savedata_size(tox.get());
    QByteArray data;
    data.resize(fileSize);
    tox_get_savedata(tox.get(), (uint8_t*)data.data());
    return data;
}

// Declared to avoid code duplication
#define GET_FRIEND_PROPERTY(property, function, checkSize)                     \
    const size_t property##Size = function##_size(tox.get(), ids[i], nullptr); \
    if ((!checkSize || property##Size) && property##Size != SIZE_MAX) {        \
        uint8_t* prop = new uint8_t[property##Size];                           \
        if (function(tox.get(), ids[i], prop, nullptr)) {                      \
            QString propStr = ToxString(prop, property##Size).getQString();    \
            emit friend##property##Changed(ids[i], propStr);                   \
        }                                                                      \
                                                                               \
        delete[] prop;                                                         \
    }

void Core::loadFriends()
{
    QMutexLocker ml{coreLoopLock.get()};

    const size_t friendCount = tox_self_get_friend_list_size(tox.get());
    if (friendCount == 0) {
        return;
    }

    uint32_t* ids = new uint32_t[friendCount];
    tox_self_get_friend_list(tox.get(), ids);
    uint8_t friendPk[TOX_PUBLIC_KEY_SIZE] = {0x00};
    for (size_t i = 0; i < friendCount; ++i) {
        if (!tox_friend_get_public_key(tox.get(), ids[i], friendPk, nullptr)) {
            continue;
        }

        emit friendAdded(ids[i], ToxPk(friendPk));
        GET_FRIEND_PROPERTY(Username, tox_friend_get_name, true);
        GET_FRIEND_PROPERTY(StatusMessage, tox_friend_get_status_message, false);
        checkLastOnline(ids[i]);
    }
    delete[] ids;
}

void Core::loadGroups()
{
    QMutexLocker ml{coreLoopLock.get()};

    const size_t groupCount = tox_conference_get_chatlist_size(tox.get());
    if (groupCount == 0) {
        return;
    }

    uint32_t* groupIds = new uint32_t[groupCount];
    tox_conference_get_chatlist(tox.get(), groupIds);

    for(size_t i = 0; i < groupCount; ++i) {
        TOX_ERR_CONFERENCE_TITLE error;
        size_t titleSize = tox_conference_get_title_size(tox.get(), groupIds[i], &error);
        if (LogConferenceTitleError(error)) {
            continue;
        }

        QByteArray name(titleSize, Qt::Uninitialized);
        if (!tox_conference_get_title(tox.get(), groupIds[i], reinterpret_cast<uint8_t*>(name.data()), &error))
        if (LogConferenceTitleError(error)) {
            continue;
        }
        emit emptyGroupCreated(static_cast<int>(groupIds[i]), getGroupPersistentId(groupIds[i]), ToxString(name).getQString());
    }

    delete[] groupIds;
}

void Core::checkLastOnline(uint32_t friendId)
{
    QMutexLocker ml{coreLoopLock.get()};

    const uint64_t lastOnline = tox_friend_get_last_online(tox.get(), friendId, nullptr);
    if (lastOnline != std::numeric_limits<uint64_t>::max()) {
        emit friendLastSeenChanged(friendId, QDateTime::fromTime_t(lastOnline));
    }
}

/**
 * @brief Returns the list of friendIds in our friendlist, an empty list on error
 */
QVector<uint32_t> Core::getFriendList() const
{
    QMutexLocker ml{coreLoopLock.get()};

    QVector<uint32_t> friends;
    friends.resize(tox_self_get_friend_list_size(tox.get()));
    tox_self_get_friend_list(tox.get(), friends.data());
    return friends;
}

/**
 * @brief Print in console text of error.
 * @param error Error to handle.
 * @return True if no error, false otherwise.
 */
bool Core::parsePeerQueryError(Tox_Err_Conference_Peer_Query error) const
{
    switch (error) {
    case TOX_ERR_CONFERENCE_PEER_QUERY_OK:
        return true;

    case TOX_ERR_CONFERENCE_PEER_QUERY_CONFERENCE_NOT_FOUND:
        qCritical() << "Conference not found";
        return false;

    case TOX_ERR_CONFERENCE_PEER_QUERY_NO_CONNECTION:
        qCritical() << "No connection";
        return false;

    case TOX_ERR_CONFERENCE_PEER_QUERY_PEER_NOT_FOUND:
        qCritical() << "Peer not found";
        return false;

    default:
        qCritical() << "Unknow error code:" << error;
        return false;
    }
}

ToxPk Core::getGroupPersistentId(uint32_t groupNumber) {
    size_t conferenceIdSize = TOX_CONFERENCE_UID_SIZE;
    QByteArray groupPersistentId(conferenceIdSize, Qt::Uninitialized);
    if (tox_conference_get_id(tox.get(), groupNumber, reinterpret_cast<uint8_t*>(groupPersistentId.data()))) {
        return ToxPk{groupPersistentId};
    } else {
        qCritical() << "Failed to get conference ID of group" << groupNumber;
        return {};
    }
}

/**
 * @brief Get number of peers in the conference.
 * @return The number of peers in the conference. UINT32_MAX on failure.
 */
uint32_t Core::getGroupNumberPeers(int groupId) const
{
    QMutexLocker ml{coreLoopLock.get()};

    Tox_Err_Conference_Peer_Query error;
    uint32_t count = tox_conference_peer_count(tox.get(), groupId, &error);
    if (!parsePeerQueryError(error)) {
        return std::numeric_limits<uint32_t>::max();
    }

    return count;
}

/**
 * @brief Get the name of a peer of a group
 */
QString Core::getGroupPeerName(int groupId, int peerId) const
{
    QMutexLocker ml{coreLoopLock.get()};

    Tox_Err_Conference_Peer_Query error;
    size_t length = tox_conference_peer_get_name_size(tox.get(), groupId, peerId, &error);
    if (!parsePeerQueryError(error)) {
        return QString{};
    }

    QByteArray name(length, Qt::Uninitialized);
    uint8_t* namePtr = reinterpret_cast<uint8_t*>(name.data());
    bool success = tox_conference_peer_get_name(tox.get(), groupId, peerId, namePtr, &error);
    if (!parsePeerQueryError(error) || !success) {
        qWarning() << "getGroupPeerName: Unknown error";
        return QString{};
    }

    return ToxString(name).getQString();
}

/**
 * @brief Get the public key of a peer of a group
 */
ToxPk Core::getGroupPeerPk(int groupId, int peerId) const
{
    QMutexLocker ml{coreLoopLock.get()};

    uint8_t friendPk[TOX_PUBLIC_KEY_SIZE] = {0x00};
    Tox_Err_Conference_Peer_Query error;
    bool success = tox_conference_peer_get_public_key(tox.get(), groupId, peerId, friendPk, &error);
    if (!parsePeerQueryError(error) || !success) {
        qWarning() << "getGroupPeerToxId: Unknown error";
        return ToxPk{};
    }

    return ToxPk(friendPk);
}

/**
 * @brief Get the names of the peers of a group
 */
QStringList Core::getGroupPeerNames(int groupId) const
{
    QMutexLocker ml{coreLoopLock.get()};

    if (!tox) {
        qWarning() << "Can't get group peer names, tox is null";
        return {};
    }

    uint32_t nPeers = getGroupNumberPeers(groupId);
    if (nPeers == std::numeric_limits<uint32_t>::max()) {
        qWarning() << "getGroupPeerNames: Unable to get number of peers";
        return {};
    }

    Tox_Err_Conference_Peer_Query error;
    uint32_t count = tox_conference_peer_count(tox.get(), groupId, &error);
    if (!parsePeerQueryError(error)) {
        return {};
    }

    if (count != nPeers) {
        qWarning() << "getGroupPeerNames: Unexpected peer count";
        return {};
    }

    QStringList names;
    for (uint32_t i = 0; i < nPeers; ++i) {
        size_t length = tox_conference_peer_get_name_size(tox.get(), groupId, i, &error);
        if (!parsePeerQueryError(error)) {
            continue;
        }

        QByteArray name(length, Qt::Uninitialized);
        uint8_t* namePtr = reinterpret_cast<uint8_t*>(name.data());
        bool ok = tox_conference_peer_get_name(tox.get(), groupId, i, namePtr, &error);
        if (ok && parsePeerQueryError(error)) {
            names.append(ToxString(name).getQString());
        }
    }

    return names;
}

/**
 * @brief Check, that group has audio or video stream
 * @param groupId Id of group to check
 * @return True for AV groups, false for text-only groups
 */
bool Core::getGroupAvEnabled(int groupId) const
{
    QMutexLocker ml{coreLoopLock.get()};
    TOX_ERR_CONFERENCE_GET_TYPE error;
    TOX_CONFERENCE_TYPE type = tox_conference_get_type(tox.get(), groupId, &error);
    switch (error) {
    case TOX_ERR_CONFERENCE_GET_TYPE_OK:
        break;
    case TOX_ERR_CONFERENCE_GET_TYPE_CONFERENCE_NOT_FOUND:
        qWarning() << "Conference not found";
        break;
    default:
        qWarning() << "Unknown error code:" << QString::number(error);
        break;
    }

    return type == TOX_CONFERENCE_TYPE_AV;
}

/**
 * @brief Print in console text of error.
 * @param error Error to handle.
 * @return True if no error, false otherwise.
 */
bool Core::parseConferenceJoinError(Tox_Err_Conference_Join error) const
{
    switch (error) {
    case TOX_ERR_CONFERENCE_JOIN_OK:
        return true;

    case TOX_ERR_CONFERENCE_JOIN_DUPLICATE:
        qCritical() << "Conference duplicate";
        return false;

    case TOX_ERR_CONFERENCE_JOIN_FAIL_SEND:
        qCritical() << "Conference join failed to send";
        return false;

    case TOX_ERR_CONFERENCE_JOIN_FRIEND_NOT_FOUND:
        qCritical() << "Friend not found";
        return false;

    case TOX_ERR_CONFERENCE_JOIN_INIT_FAIL:
        qCritical() << "Init fail";
        return false;

    case TOX_ERR_CONFERENCE_JOIN_INVALID_LENGTH:
        qCritical() << "Invalid length";
        return false;

    case TOX_ERR_CONFERENCE_JOIN_WRONG_TYPE:
        qCritical() << "Wrong conference type";
        return false;

    default:
        qCritical() << "Unknow error code:" << error;
        return false;
    }
}

/**
 * @brief Accept a groupchat invite.
 * @param inviteInfo Object which contains info about group invitation
 *
 * @return Conference number on success, UINT32_MAX on failure.
 */
uint32_t Core::joinGroupchat(const GroupInvite& inviteInfo) const
{
    QMutexLocker ml{coreLoopLock.get()};

    const uint32_t friendId = inviteInfo.getFriendId();
    const uint8_t confType = inviteInfo.getType();
    const QByteArray invite = inviteInfo.getInvite();
    const uint8_t* const cookie = reinterpret_cast<const uint8_t*>(invite.data());
    const size_t cookieLength = invite.length();
    switch (confType) {
    case TOX_CONFERENCE_TYPE_TEXT: {
        qDebug() << QString("Trying to join text groupchat invite sent by friend %1").arg(friendId);
        Tox_Err_Conference_Join error;
        uint32_t groupId = tox_conference_join(tox.get(), friendId, cookie, cookieLength, &error);
        return parseConferenceJoinError(error) ? groupId : std::numeric_limits<uint32_t>::max();
    }

    case TOX_CONFERENCE_TYPE_AV: {
        qDebug() << QString("Trying to join AV groupchat invite sent by friend %1").arg(friendId);
        return toxav_join_av_groupchat(tox.get(), friendId, cookie, cookieLength,
                                       CoreAV::groupCallCallback, const_cast<Core*>(this));
    }

    default:
        qWarning() << "joinGroupchat: Unknown groupchat type " << confType;
    }

    return std::numeric_limits<uint32_t>::max();
}

void Core::groupInviteFriend(uint32_t friendId, int groupId)
{
    QMutexLocker ml{coreLoopLock.get()};

    Tox_Err_Conference_Invite error;
    tox_conference_invite(tox.get(), friendId, groupId, &error);

    switch (error) {
    case TOX_ERR_CONFERENCE_INVITE_OK:
        break;

    case TOX_ERR_CONFERENCE_INVITE_CONFERENCE_NOT_FOUND:
        qCritical() << "Conference not found";
        break;

    case TOX_ERR_CONFERENCE_INVITE_FAIL_SEND:
        qCritical() << "Conference invite failed to send";
        break;

    default:
        break;
    }
}

int Core::createGroup(uint8_t type)
{
    QMutexLocker ml{coreLoopLock.get()};

    if (type == TOX_CONFERENCE_TYPE_TEXT) {
        Tox_Err_Conference_New error;
        uint32_t groupId = tox_conference_new(tox.get(), &error);

        switch (error) {
        case TOX_ERR_CONFERENCE_NEW_OK:
            emit emptyGroupCreated(groupId, getGroupPersistentId(groupId));
            return groupId;

        case TOX_ERR_CONFERENCE_NEW_INIT:
            qCritical() << "The conference instance failed to initialize";
            return std::numeric_limits<uint32_t>::max();

        default:
            return std::numeric_limits<uint32_t>::max();
        }
    } else if (type == TOX_CONFERENCE_TYPE_AV) {
        uint32_t groupId = toxav_add_av_groupchat(tox.get(), CoreAV::groupCallCallback, this);
        emit emptyGroupCreated(groupId, getGroupPersistentId(groupId));
        return groupId;
    } else {
        qWarning() << "createGroup: Unknown type " << type;
        return -1;
    }
}

/**
 * @brief Checks if a friend is online. Unknown friends are considered offline.
 */
bool Core::isFriendOnline(uint32_t friendId) const
{
    QMutexLocker ml{coreLoopLock.get()};

    Tox_Connection connetion = tox_friend_get_connection_status(tox.get(), friendId, nullptr);
    return connetion != TOX_CONNECTION_NONE;
}

/**
 * @brief Checks if we have a friend by public key
 */
bool Core::hasFriendWithPublicKey(const ToxPk& publicKey) const
{
    QMutexLocker ml{coreLoopLock.get()};

    if (publicKey.isEmpty()) {
        return false;
    }

    // TODO: error handling
    uint32_t friendId = tox_friend_by_public_key(tox.get(), publicKey.getBytes(), nullptr);
    return friendId != std::numeric_limits<uint32_t>::max();
}

/**
 * @brief Get the public key part of the ToxID only
 */
ToxPk Core::getFriendPublicKey(uint32_t friendNumber) const
{
    QMutexLocker ml{coreLoopLock.get()};

    uint8_t rawid[TOX_PUBLIC_KEY_SIZE];
    if (!tox_friend_get_public_key(tox.get(), friendNumber, rawid, nullptr)) {
        qWarning() << "getFriendPublicKey: Getting public key failed";
        return ToxPk();
    }

    return ToxPk(rawid);
}

/**
 * @brief Get the username of a friend
 */
QString Core::getFriendUsername(uint32_t friendnumber) const
{
    QMutexLocker ml{coreLoopLock.get()};

    size_t namesize = tox_friend_get_name_size(tox.get(), friendnumber, nullptr);
    if (namesize == SIZE_MAX) {
        qWarning() << "getFriendUsername: Failed to get name size for friend " << friendnumber;
        return QString();
    }

    uint8_t* name = new uint8_t[namesize];
    tox_friend_get_name(tox.get(), friendnumber, name, nullptr);
    ToxString sname(name, namesize);
    delete[] name;
    return sname.getQString();
}

QStringList Core::splitMessage(const QString& message, int maxLen)
{
    QStringList splittedMsgs;
    QByteArray ba_message{message.toUtf8()};

    while (ba_message.size() > maxLen) {
        int splitPos = ba_message.lastIndexOf('\n', maxLen - 1);

        if (splitPos <= 0) {
            splitPos = ba_message.lastIndexOf(' ', maxLen - 1);
        }

        if (splitPos <= 0) {
            constexpr uint8_t firstOfMultiByteMask = 0xC0;
            constexpr uint8_t multiByteMask = 0x80;
            splitPos = maxLen;
            // don't split a utf8 character
            if ((ba_message[splitPos] & multiByteMask) == multiByteMask) {
                while ((ba_message[splitPos] & firstOfMultiByteMask) != firstOfMultiByteMask) {
                    --splitPos;
                }
            }
            --splitPos;
        }
        splittedMsgs.append(QString{ba_message.left(splitPos + 1)});
        ba_message = ba_message.mid(splitPos + 1);
    }

    splittedMsgs.append(QString{ba_message});
    return splittedMsgs;
}

QString Core::getPeerName(const ToxPk& id) const
{
    QMutexLocker ml{coreLoopLock.get()};

    QString name;
    uint32_t friendId = tox_friend_by_public_key(tox.get(), id.getBytes(), nullptr);
    if (friendId == std::numeric_limits<uint32_t>::max()) {
        qWarning() << "getPeerName: No such peer";
        return name;
    }

    const size_t nameSize = tox_friend_get_name_size(tox.get(), friendId, nullptr);
    if (nameSize == SIZE_MAX) {
        return name;
    }

    uint8_t* cname = new uint8_t[nameSize < tox_max_name_length() ? tox_max_name_length() : nameSize];
    if (!tox_friend_get_name(tox.get(), friendId, cname, nullptr)) {
        qWarning() << "getPeerName: Can't get name of friend " + QString().setNum(friendId);
        delete[] cname;
        return name;
    }

    name = ToxString(cname, nameSize).getQString();
    delete[] cname;
    return name;
}

/**
 * @brief Most of the API shouldn't be used until Core is ready, call start() first
 */
bool Core::isReady() const
{
    return av && tox;
}

/**
 * @brief Sets the NoSpam value to prevent friend request spam
 * @param nospam an arbitrary which becomes part of the Tox ID
 */
void Core::setNospam(uint32_t nospam)
{
    QMutexLocker ml{coreLoopLock.get()};

    tox_self_set_nospam(tox.get(), nospam);
    emit idSet(getSelfId());
}
