/*
    Copyright © 2013 by Maxim Biro <nurupo.contributions@gmail.com>
    Copyright © 2014-2019 by The qTox Project Contributors

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

#include "coreav.h"
#include "audio/iaudiosettings.h"
#include "core.h"
#include "src/model/friend.h"
#include "src/model/group.h"
#include "src/persistence/igroupsettings.h"
#include "src/video/corevideosource.h"
#include "src/video/videoframe.h"
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <cassert>

/**
 * @fn void CoreAV::avInvite(uint32_t friendId, bool video)
 * @brief Sent when a friend calls us.
 * @param friendId Id of friend in call list.
 * @param video False if chat is audio only, true audio and video.
 *
 * @fn void CoreAV::avStart(uint32_t friendId, bool video)
 * @brief Sent when a call we initiated has started.
 * @param friendId Id of friend in call list.
 * @param video False if chat is audio only, true audio and video.
 *
 * @fn void CoreAV::avEnd(uint32_t friendId)
 * @brief Sent when a call was ended by the peer.
 * @param friendId Id of friend in call list.
 *
 * @var CoreAV::VIDEO_DEFAULT_BITRATE
 * @brief Picked at random by fair dice roll.
 */

/**
 * @var std::atomic_flag CoreAV::threadSwitchLock
 * @brief This flag is to be acquired before switching in a blocking way between the UI and CoreAV
 * thread.
 *
 * The CoreAV thread must have priority for the flag, other threads should back off or release it
 * quickly.
 * CoreAV needs to interface with three threads, the toxcore/Core thread that fires non-payload
 * toxav callbacks, the toxav/CoreAV thread that fires AV payload callbacks and manages
 * most of CoreAV's members, and the UI thread, which calls our [start/answer/cancel]Call functions
 * and which we call via signals.
 * When the UI calls us, we switch from the UI thread to the CoreAV thread to do the processing,
 * when toxcore fires a non-payload av callback, we do the processing in the CoreAV thread and then
 * switch to the UI thread to send it a signal. Both switches block both threads, so this would
 * deadlock.
 */

CoreAV::CoreAV(std::unique_ptr<ToxAV, ToxAVDeleter> toxav, QMutex& toxCoreLock,
               IAudioSettings& _audioSettings, IGroupSettings& _groupSettings)
    : audio{nullptr}
    , toxav{std::move(toxav)}
    , coreavThread{new QThread{this}}
    , iterateTimer{new QTimer{this}}
    , coreLock{toxCoreLock}
    , audioSettings{_audioSettings}
    , groupSettings{_groupSettings}
{
    assert(coreavThread);
    assert(iterateTimer);

    coreavThread->setObjectName("qTox CoreAV");
    moveToThread(coreavThread.get());

    connectCallbacks(*this->toxav);

    iterateTimer->setSingleShot(true);

    connect(iterateTimer, &QTimer::timeout, this, &CoreAV::process);
    connect(coreavThread.get(), &QThread::finished, iterateTimer, &QTimer::stop);
    connect(coreavThread.get(), &QThread::started, this, &CoreAV::process);
    qRegisterMetaType<ToxFriendCallPtr>("ToxFriendCallPtr");
}

void CoreAV::connectCallbacks(ToxAV& toxav)
{
    toxav_callback_call(&toxav, CoreAV::callCallback, this);
    toxav_callback_call_state(&toxav, CoreAV::stateCallback, this);
    toxav_callback_audio_bit_rate(&toxav, CoreAV::audioBitrateCallback, this);
    toxav_callback_video_bit_rate(&toxav, CoreAV::videoBitrateCallback, this);
    toxav_callback_audio_receive_frame(&toxav, CoreAV::audioFrameCallback, this);
    toxav_callback_video_receive_frame(&toxav, CoreAV::videoFrameCallback, this);
}

/**
 * @brief Factory method for CoreAV
 * @param core pointer to the Tox instance
 * @return CoreAV instance on success, {} on failure
 */
CoreAV::CoreAVPtr CoreAV::makeCoreAV(Tox* core, QMutex& toxCoreLock,
                                     IAudioSettings& audioSettings, IGroupSettings& groupSettings)
{
    Toxav_Err_New err;
    std::unique_ptr<ToxAV, ToxAVDeleter> toxav{toxav_new(core, &err)};
    switch (err) {
    case TOXAV_ERR_NEW_OK:
        break;
    case TOXAV_ERR_NEW_MALLOC:
        qCritical() << "Failed to allocate resources for ToxAV";
        return {};
    case TOXAV_ERR_NEW_MULTIPLE:
        qCritical() << "Attempted to create multiple ToxAV instances";
        return {};
    case TOXAV_ERR_NEW_NULL:
        qCritical() << "Unexpected NULL parameter";
        return {};
    }

    assert(toxav != nullptr);

    return CoreAVPtr{new CoreAV{std::move(toxav), toxCoreLock, audioSettings, groupSettings}};
}

/**
 * @brief Set the audio backend
 * @param audio The audio backend to use
 * @note This must be called before starting CoreAV and audio must outlive CoreAV
 */
void CoreAV::setAudio(IAudioControl& newAudio)
{
    audio.exchange(&newAudio);
}

/**
 * @brief Get the audio backend used
 * @return Pointer to the audio backend
 * @note This is needed only for the case CoreAV needs to restart and the restarting class doesn't
 * have access to the audio backend and wants to keep it the same.
 */
IAudioControl* CoreAV::getAudio()
{
    return audio;
}

CoreAV::~CoreAV()
{
    /* Gracefully leave calls and group calls to avoid deadlocks in destructor */
    for (const auto& call : calls) {
        cancelCall(call.first);
    }
    for (const auto& call : groupCalls) {
        leaveGroupCall(call.first);
    }

    assert(calls.empty());
    assert(groupCalls.empty());

    coreavThread->exit(0);
    coreavThread->wait();
}

/**
 * @brief Starts the CoreAV main loop that calls toxav's main loop
 */
void CoreAV::start()
{
    coreavThread->start();
}

void CoreAV::process()
{
    assert(QThread::currentThread() == coreavThread.get());
    toxav_iterate(toxav.get());
    iterateTimer->start(toxav_iteration_interval(toxav.get()));
}

/**
 * @brief Checks the call status for a Tox group.
 * @param g the group to check
 * @return true, if the call is active for the group, false otherwise
 */
bool CoreAV::isCallActive(const Group* g) const
{
    QReadLocker locker{&callsLock};
    auto it = groupCalls.find(g->getId());
    if (it == groupCalls.end()) {
        return false;
    }
    return it->second->isActive();
}

bool CoreAV::answerCall(uint32_t friendNum, bool video)
{
    QWriteLocker locker{&callsLock};
    QMutexLocker coreLocker{&coreLock};

    qDebug() << QString("Answering call %1").arg(friendNum);
    auto it = calls.find(friendNum);
    assert(it != calls.end());
    Toxav_Err_Answer err;

    const uint32_t videoBitrate = video ? VIDEO_DEFAULT_BITRATE : 0;
    if (toxav_answer(toxav.get(), friendNum, audioSettings.getAudioBitrate(),
                     videoBitrate, &err)) {
        it->second->setActive(true);
        return true;
    } else {
        qWarning() << "Failed to answer call with error" << err;
        toxav_call_control(toxav.get(), friendNum, TOXAV_CALL_CONTROL_CANCEL, nullptr);
        return false;
    }
}

CoreAV::ToxFriendCallPtr CoreAV::startCall(uint32_t friendNum, bool video)
{
    QWriteLocker locker{&callsLock};
    QMutexLocker coreLocker{&coreLock};

    qDebug() << QString("Starting call with %1").arg(friendNum);
    auto it = calls.find(friendNum);
    if (it != calls.end()) {
        qWarning() << QString("Can't start call with %1, we're already in this call!").arg(friendNum);
        return {};
    }

    uint32_t videoBitrate = video ? VIDEO_DEFAULT_BITRATE : 0;
    if (!toxav_call(toxav.get(), friendNum, audioSettings.getAudioBitrate(), videoBitrate,
                    nullptr))
        return {};

    // Audio backend must be set before making a call
    assert(audio != nullptr);
    ToxFriendCallPtr call = ToxFriendCallPtr(new ToxFriendCall(friendNum, video, *this, *audio));
    // Call object must be owned by this thread or there will be locking problems with Audio
    call->moveToThread(this->thread());
    assert(call != nullptr);
    calls.emplace(friendNum, call.get());
    return call;
}

bool CoreAV::cancelCall(uint32_t friendNum)
{
    QWriteLocker locker{&callsLock};
    QMutexLocker coreLocker{&coreLock};

    auto it = calls.find(friendNum);
    if (it == calls.end()) {
        return false;
    }

    qDebug() << QString("Cancelling call with %1").arg(friendNum);
    if (!toxav_call_control(toxav.get(), friendNum, TOXAV_CALL_CONTROL_CANCEL, nullptr)) {
        qWarning() << QString("Failed to cancel call with %1").arg(friendNum);
        return false;
    }

    calls.erase(friendNum);
    locker.unlock();

    emit avEnd(friendNum);
    return true;
}

/**
 * @brief Send audio frame to a friend
 * @param callId Id of friend in call list.
 * @param pcm An array of audio samples (Pulse-code modulation).
 * @param samples Number of samples in this frame.
 * @param chans Number of audio channels.
 * @param rate Audio sampling rate used in this frame.
 * @return False only on error, but not if there's nothing to send.
 */
bool CoreAV::sendCallAudio(uint32_t callId, const int16_t* pcm, size_t samples, uint8_t chans,
                           uint32_t rate) const
{
    QReadLocker locker{&callsLock};

    auto it = calls.find(callId);
    if (it == calls.end()) {
        return false;
    }

    ToxFriendCall const& call = *it->second;

    if (call.getMuteMic() || !call.isActive()
        || !(call.getState() & TOXAV_FRIEND_CALL_STATE_ACCEPTING_A)) {
        return true;
    }

    // TOXAV_ERR_SEND_FRAME_SYNC means toxav failed to lock, retry 5 times in this case
    Toxav_Err_Send_Frame err;
    int retries = 0;
    do {
        if (!toxav_audio_send_frame(toxav.get(), callId, pcm, samples, chans, rate, &err)) {
            if (err == TOXAV_ERR_SEND_FRAME_SYNC) {
                ++retries;
                QThread::usleep(500);
            } else {
                qDebug() << "toxav_audio_send_frame error: " << err;
            }
        }
    } while (err == TOXAV_ERR_SEND_FRAME_SYNC && retries < 5);
    if (err == TOXAV_ERR_SEND_FRAME_SYNC) {
        qDebug() << "toxav_audio_send_frame error: Lock busy, dropping frame";
    }

    return true;
}

void CoreAV::sendCallVideo(uint32_t callId, std::shared_ptr<VideoFrame> vframe)
{
    QWriteLocker locker{&callsLock};

    // We might be running in the FFmpeg thread and holding the CameraSource lock
    // So be careful not to deadlock with anything while toxav locks in toxav_video_send_frame
    auto it = calls.find(callId);
    if (it == calls.end()) {
        return;
    }

    ToxFriendCall& call = *it->second;

    if (!call.getVideoEnabled() || !call.isActive()
        || !(call.getState() & TOXAV_FRIEND_CALL_STATE_ACCEPTING_V)) {
        return;
    }

    if (call.getNullVideoBitrate()) {
        qDebug() << "Restarting video stream to friend" << callId;
        QMutexLocker coreLocker{&coreLock};
        toxav_video_set_bit_rate(toxav.get(), callId, VIDEO_DEFAULT_BITRATE, nullptr);
        call.setNullVideoBitrate(false);
    }

    ToxYUVFrame frame = vframe->toToxYUVFrame();

    if (!frame) {
        return;
    }

    // TOXAV_ERR_SEND_FRAME_SYNC means toxav failed to lock, retry 5 times in this case
    // We don't want to be dropping iframes because of some lock held by toxav_iterate
    Toxav_Err_Send_Frame err;
    int retries = 0;
    do {
        if (!toxav_video_send_frame(toxav.get(), callId, frame.width, frame.height, frame.y,
                                    frame.u, frame.v, &err)) {
            if (err == TOXAV_ERR_SEND_FRAME_SYNC) {
                ++retries;
                QThread::usleep(500);
            } else {
                qDebug() << "toxav_video_send_frame error: " << err;
            }
        }
    } while (err == TOXAV_ERR_SEND_FRAME_SYNC && retries < 5);
    if (err == TOXAV_ERR_SEND_FRAME_SYNC) {
        qDebug() << "toxav_video_send_frame error: Lock busy, dropping frame";
    }
}

/**
 * @brief Called from Tox API when group call receives audio data.
 *
 * @param[in] tox          the Tox object
 * @param[in] group        the group number
 * @param[in] peer         the peer number
 * @param[in] data         the audio data to playback
 * @param[in] samples      the audio samples
 * @param[in] channels     the audio channels
 * @param[in] sample_rate  the audio sample rate
 * @param[in] core         the qTox Core class
 */
void CoreAV::groupCallCallback(void* tox, uint32_t group, uint32_t peer, const int16_t* data,
                               unsigned samples, uint8_t channels, uint32_t sample_rate, void* core)
{
    /*
     * Currently group call audio decoding is handled in the Tox thread by c-toxcore,
     * so we can be sure that this function is always called from the Core thread.
     * To change this, an API change in c-toxcore is needed and this function probably must be
     * changed.
     * See https://github.com/TokTok/c-toxcore/issues/1364 for details.
     */

    Q_UNUSED(tox)
    Core* c = static_cast<Core*>(core);
    CoreAV* cav = c->getAv();

    QReadLocker locker{&cav->callsLock};

    const ToxPk peerPk = c->getGroupPeerPk(group, peer);
    // don't play the audio if it comes from a muted peer
    if (cav->groupSettings.getBlackList().contains(peerPk.toString())) {
        return;
    }

    emit c->groupPeerAudioPlaying(group, peerPk);

    auto it = cav->groupCalls.find(group);
    if (it == cav->groupCalls.end()) {
        return;
    }

    auto& call = *it->second;

    if (call.getMuteVol() || !call.isActive()) {
        return;
    }

    call.playAudioBuffer(peerPk, data, samples, channels, sample_rate);
}

/**
 * @brief Starts a call in an existing AV groupchat.
 * @note Call from the GUI thread.
 * @param groupId Id of group to join
 */
CoreAV::ToxGroupCallPtr CoreAV::joinGroupCall(const Group& group)
{
    QWriteLocker locker{&callsLock};

    qDebug() << QString("Joining group call %1").arg(group.getId());

    // Audio backend must be set before starting a call
    assert(audio != nullptr);

    ToxGroupCallPtr groupcall = ToxGroupCallPtr(new ToxGroupCall{group, *this, *audio});
    // Call Objects must be owned by CoreAV or there will be locking problems with Audio
    assert(groupcall != nullptr);
    groupcall->moveToThread(this->thread());
    auto ret = groupCalls.emplace(group.getId(), groupcall.get());
    if (ret.second == false) {
        qWarning() << "This group call already exists, not joining!";
        return {};
    }
    groupcall->setActive(true);
    return groupcall;
}

/**
 * @brief Will not leave the group, just stop the call.
 * @note Call from the GUI thread.
 * @param groupId Id of group to leave
 */
void CoreAV::leaveGroupCall(int groupId)
{
    QWriteLocker locker{&callsLock};

    qDebug() << QString("Leaving group call %1").arg(groupId);

    groupCalls.erase(groupId);
}

bool CoreAV::sendGroupCallAudio(int groupId, const int16_t* pcm, size_t samples, uint8_t chans,
                                uint32_t rate) const
{
    QReadLocker locker{&callsLock};

    auto it = groupCalls.find(groupId);
    if (it == groupCalls.end()) {
        return false;
    }

    if (!it->second->isActive() || it->second->getMuteMic()) {
        return true;
    }

    if (toxav_group_send_audio(toxav_get_tox(toxav.get()), groupId, pcm, samples, chans, rate) != 0)
        qDebug() << "toxav_group_send_audio error";

    return true;
}

/**
 * @brief Signal to all peers that we're not sending video anymore.
 * @note The next frame sent cancels this.
 */
void CoreAV::sendNoVideo()
{
    QWriteLocker locker{&callsLock};

    // We don't change the audio bitrate, but we signal that we're not sending video anymore
    qDebug() << "CoreAV: Signaling end of video sending";
    for (auto& kv : calls) {
        ToxFriendCall& call = *kv.second;
        toxav_video_set_bit_rate(toxav.get(), kv.first, 0, nullptr);
        call.setNullVideoBitrate(true);
    }
}

void CoreAV::callCallback(ToxAV* toxav, uint32_t friendNum, bool audio, bool video, void* vSelf)
{
    CoreAV* self = static_cast<CoreAV*>(vSelf);

    QWriteLocker locker{&self->callsLock};

    // Audio backend must be set before receiving a call
    assert(self->audio != nullptr);
    ToxFriendCallPtr call = ToxFriendCallPtr(new ToxFriendCall{friendNum, video, *self, *self->audio});
    // Call object must be owned by CoreAV thread or there will be locking problems with Audio
    call->moveToThread(self->thread());
    assert(call != nullptr);

    auto it = self->calls.emplace(friendNum, call.get());
    if (it.second == false) {
        qWarning() << QString("Rejecting call invite from %1, we're already in that call!").arg(friendNum);
        toxav_call_control(toxav, friendNum, TOXAV_CALL_CONTROL_CANCEL, nullptr);
        return;
    }
    qDebug() << QString("Received call invite from %1").arg(friendNum);

    // We don't get a state callback when answering, so fill the state ourselves in advance
    int state = 0;
    if (audio)
        state |= TOXAV_FRIEND_CALL_STATE_SENDING_A | TOXAV_FRIEND_CALL_STATE_ACCEPTING_A;
    if (video)
        state |= TOXAV_FRIEND_CALL_STATE_SENDING_V | TOXAV_FRIEND_CALL_STATE_ACCEPTING_V;
    it.first->second->setState(static_cast<TOXAV_FRIEND_CALL_STATE>(state));

    // Must explicitely unlock, because a deadlock can happen via ChatForm/Audio
    locker.unlock();

    emit self->avInvite(friendNum, video, call);
}

void CoreAV::stateCallback(ToxAV* toxav, uint32_t friendNum, uint32_t state, void* vSelf)
{
    Q_UNUSED(toxav)
    CoreAV* self = static_cast<CoreAV*>(vSelf);

    // we must unlock this lock before emitting any signals
    QWriteLocker locker{&self->callsLock};

    auto it = self->calls.find(friendNum);
    if (it == self->calls.end()) {
        qWarning() << QString("stateCallback called, but call %1 is already dead").arg(friendNum);
        return;
    }

    ToxFriendCall& call = *it->second;

    if (state & TOXAV_FRIEND_CALL_STATE_ERROR) {
        qWarning() << "Call with friend" << friendNum << "died of unnatural causes!";
        self->calls.erase(friendNum);
        locker.unlock();
        emit self->avEnd(friendNum, true);
    } else if (state & TOXAV_FRIEND_CALL_STATE_FINISHED) {
        qDebug() << "Call with friend" << friendNum << "finished quietly";
        self->calls.erase(friendNum);
        locker.unlock();
        emit self->avEnd(friendNum);
    } else {
        // If our state was null, we started the call and were still ringing
        if (!call.getState() && state) {
            call.setActive(true);
            bool videoEnabled = call.getVideoEnabled();
            call.setState(static_cast<TOXAV_FRIEND_CALL_STATE>(state));
            locker.unlock();
            emit self->avStart(friendNum, videoEnabled);
        } else if ((call.getState() & TOXAV_FRIEND_CALL_STATE_SENDING_V)
                   && !(state & TOXAV_FRIEND_CALL_STATE_SENDING_V)) {
            qDebug() << "Friend" << friendNum << "stopped sending video";
            if (call.getVideoSource()) {
                call.getVideoSource()->stopSource();
            }

            call.setState(static_cast<TOXAV_FRIEND_CALL_STATE>(state));
        } else if (!(call.getState() & TOXAV_FRIEND_CALL_STATE_SENDING_V)
                   && (state & TOXAV_FRIEND_CALL_STATE_SENDING_V)) {
            // Workaround toxav sometimes firing callbacks for "send last frame" -> "stop sending
            // video"
            // out of orders (even though they were sent in order by the other end).
            // We simply stop the videoSource from emitting anything while the other end says it's
            // not sending
            if (call.getVideoSource()) {
                call.getVideoSource()->restartSource();
            }

            call.setState(static_cast<TOXAV_FRIEND_CALL_STATE>(state));
        }
    }
}

// This is only a dummy implementation for now
void CoreAV::bitrateCallback(ToxAV* toxav, uint32_t friendNum, uint32_t arate, uint32_t vrate,
                             void* vSelf)
{
    CoreAV* self = static_cast<CoreAV*>(vSelf);
    Q_UNUSED(self)
    Q_UNUSED(toxav)

    qDebug() << "Recommended bitrate with" << friendNum << " is now " << arate << "/" << vrate
             << ", ignoring it";
}

// This is only a dummy implementation for now
void CoreAV::audioBitrateCallback(ToxAV* toxav, uint32_t friendNum, uint32_t rate, void* vSelf)
{
    CoreAV* self = static_cast<CoreAV*>(vSelf);
    Q_UNUSED(self)
    Q_UNUSED(toxav)

    qDebug() << "Recommended audio bitrate with" << friendNum << " is now " << rate << ", ignoring it";
}

// This is only a dummy implementation for now
void CoreAV::videoBitrateCallback(ToxAV* toxav, uint32_t friendNum, uint32_t rate, void* vSelf)
{
    CoreAV* self = static_cast<CoreAV*>(vSelf);
    Q_UNUSED(self)
    Q_UNUSED(toxav)

    qDebug() << "Recommended video bitrate with" << friendNum << " is now " << rate << ", ignoring it";
}

void CoreAV::audioFrameCallback(ToxAV*, uint32_t friendNum, const int16_t* pcm, size_t sampleCount,
                                uint8_t channels, uint32_t samplingRate, void* vSelf)
{
    CoreAV* self = static_cast<CoreAV*>(vSelf);
    // This callback should come from the CoreAV thread
    assert(QThread::currentThread() == self->coreavThread.get());
    QReadLocker locker{&self->callsLock};

    auto it = self->calls.find(friendNum);
    if (it == self->calls.end()) {
        return;
    }

    ToxFriendCall& call = *it->second;

    if (call.getMuteVol()) {
        return;
    }

    call.playAudioBuffer(pcm, sampleCount, channels, samplingRate);
}

void CoreAV::videoFrameCallback(ToxAV*, uint32_t friendNum, uint16_t w, uint16_t h,
                                const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                int32_t ystride, int32_t ustride, int32_t vstride, void* vSelf)
{
    auto self = static_cast<CoreAV*>(vSelf);
    // This callback should come from the CoreAV thread
    assert(QThread::currentThread() == self->coreavThread.get());
    QReadLocker locker{&self->callsLock};

    auto it = self->calls.find(friendNum);
    if (it == self->calls.end()) {
        return;
    }

    CoreVideoSource* videoSource = it->second->getVideoSource();
    if (!videoSource) {
        return;
    }

    vpx_image frame;
    frame.d_h = h;
    frame.d_w = w;
    frame.planes[0] = const_cast<uint8_t*>(y);
    frame.planes[1] = const_cast<uint8_t*>(u);
    frame.planes[2] = const_cast<uint8_t*>(v);
    frame.stride[0] = ystride;
    frame.stride[1] = ustride;
    frame.stride[2] = vstride;

    videoSource->pushFrame(&frame);
}
