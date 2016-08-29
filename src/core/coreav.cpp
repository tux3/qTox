/*
    Copyright (C) 2013 by Maxim Biro <nurupo.contributions@gmail.com>
    Copyright © 2014-2015 by The qTox Project

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
#include "coreav.h"
#include "audio/audio.h"
#include "friend.h"
#include "group.h"
#include "persistence/settings.h"
#include "video/videoframe.h"
#include "video/corevideosource.h"
#include <cassert>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrentRun>

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
 * @var CoreAV::AUDIO_DEFAULT_BITRATE
 * @brief In kb/s. More than enough for Opus.
 *
 * @var CoreAV::VIDEO_DEFAULT_BITRATE
 * @brief Picked at random by fair dice roll.
 */

/**
 * @var std::atomic_flag CoreAV::threadSwitchLock
 * @brief This flag is to be acquired before switching in a blocking way between the UI and CoreAV thread.
 *
 * The CoreAV thread must have priority for the flag, other threads should back off or release it quickly.
 * CoreAV needs to interface with three threads, the toxcore/Core thread that fires non-payload
 * toxav callbacks, the toxav/CoreAV thread that fires AV payload callbacks and manages
 * most of CoreAV's members, and the UI thread, which calls our [start/answer/cancel]Call functions
 * and which we call via signals.
 * When the UI calls us, we switch from the UI thread to the CoreAV thread to do the processing,
 * when toxcore fires a non-payload av callback, we do the processing in the CoreAV thread and then
 * switch to the UI thread to send it a signal. Both switches block both threads, so this would deadlock.
 */

/**
 * @brief Maps friend IDs to ToxFriendCall.
 */
IndexedList<ToxFriendCall> CoreAV::calls;

/**
 * @brief Maps group IDs to ToxGroupCalls.
 */
IndexedList<ToxGroupCall> CoreAV::groupCalls;

CoreAV::CoreAV(Tox *tox)
    : coreavThread{new QThread}, iterateTimer{new QTimer{this}},
      threadSwitchLock{false}
{
    coreavThread->setObjectName("qTox CoreAV");
    moveToThread(coreavThread.get());

    iterateTimer->setSingleShot(true);
    connect(iterateTimer.get(), &QTimer::timeout, this, &CoreAV::process);

    toxav = toxav_new(tox, nullptr);

    toxav_callback_call(toxav, CoreAV::callCallback, this);
    toxav_callback_call_state(toxav, CoreAV::stateCallback, this);
    toxav_callback_bit_rate_status(toxav, CoreAV::bitrateCallback, this);
    toxav_callback_audio_receive_frame(toxav, CoreAV::audioFrameCallback, this);
    toxav_callback_video_receive_frame(toxav, CoreAV::videoFrameCallback, this);

    coreavThread->start();
}

CoreAV::~CoreAV()
{
    for (const ToxFriendCall& call : calls)
        cancelCall(call.callId);
    killTimerFromThread();
    toxav_kill(toxav);
    coreavThread->exit(0);
    while (coreavThread->isRunning())
    {
        qApp->processEvents();
        coreavThread->wait(100);
    }
}

const ToxAV *CoreAV::getToxAv() const
{
    return toxav;
}

/**
 * @brief Starts the CoreAV main loop that calls toxav's main loop
 */
void CoreAV::start()
{
    // Timers can only be touched from their own thread
    if (QThread::currentThread() != coreavThread.get())
        return (void)QMetaObject::invokeMethod(this, "start", Qt::BlockingQueuedConnection);
    iterateTimer->start();
}

/**
 * @brief Stops the main loop
 */
void CoreAV::stop()
{
    // Timers can only be touched from their own thread
    if (QThread::currentThread() != coreavThread.get())
        return (void)QMetaObject::invokeMethod(this, "stop", Qt::BlockingQueuedConnection);
    iterateTimer->stop();
}

/**
 * @brief Calls itself blocking queued on the coreav thread
 */
void CoreAV::killTimerFromThread()
{
    // Timers can only be touched from their own thread
    if (QThread::currentThread() != coreavThread.get())
        return (void)QMetaObject::invokeMethod(this, "killTimerFromThread", Qt::BlockingQueuedConnection);
    iterateTimer.release();
}

void CoreAV::process()
{
    toxav_iterate(toxav);
    iterateTimer->start(toxav_iteration_interval(toxav));
}

/**
 * @brief Check, if any calls are currently active.
 * @return true if any calls are currently active, false otherwise
 * @note A call about to start is not yet active.
 */
bool CoreAV::anyActiveCalls() const
{
    return !calls.isEmpty();
}

/**
 * @brief Checks the call status for a Tox friend.
 * @param f the friend to check
 * @return true, if call is active for the friend, false otherwise
 */
bool CoreAV::isCallActive(const Friend* f) const
{
    return f && calls.contains(f->getFriendID())
            ? !(calls[f->getFriendID()].inactive)
            : false;
}

/**
 * @brief Checks the call status for a Tox group.
 * @param g the group to check
 * @return true, if the call is active for the group, false otherwise
 */
bool CoreAV::isCallActive(const Group* g) const
{
    return g && groupCalls.contains(g->getGroupId())
            ? !(groupCalls[g->getGroupId()].inactive)
            : false;
}

bool CoreAV::isCallVideoEnabled(const Friend* f) const
{
    return f && calls.contains(f->getFriendID())
            ? calls[f->getFriendID()].videoEnabled
            : false;
}

bool CoreAV::answerCall(uint32_t friendNum)
{
    if (QThread::currentThread() != coreavThread.get())
    {
        if (threadSwitchLock.test_and_set(std::memory_order_acquire))
        {
            qDebug() << "CoreAV::answerCall: Backed off of thread-switch lock";
            return false;
        }

        bool ret;
        QMetaObject::invokeMethod(this, "answerCall", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret), Q_ARG(uint32_t, friendNum));

        threadSwitchLock.clear(std::memory_order_release);
        return ret;
    }

    qDebug() << QString("answering call %1").arg(friendNum);
    assert(calls.contains(friendNum));
    TOXAV_ERR_ANSWER err;
    if (toxav_answer(toxav, friendNum, AUDIO_DEFAULT_BITRATE, VIDEO_DEFAULT_BITRATE, &err))
    {
        calls[friendNum].inactive = false;
        return true;
    }
    else
    {
        qWarning() << "Failed to answer call with error"<<err;
        toxav_call_control(toxav, friendNum, TOXAV_CALL_CONTROL_CANCEL, nullptr);
        calls.remove(friendNum);
        return false;
    }
}

bool CoreAV::startCall(uint32_t friendNum, bool video)
{
    if (QThread::currentThread() != coreavThread.get())
    {
        if (threadSwitchLock.test_and_set(std::memory_order_acquire))
        {
            qDebug() << "CoreAV::startCall: Backed off of thread-switch lock";
            return false;
        }

        bool ret;
        QMetaObject::invokeMethod(this, "startCall", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret),
                                  Q_ARG(uint32_t, friendNum),
                                  Q_ARG(bool, video));

        threadSwitchLock.clear(std::memory_order_release);
        return ret;
    }

    qDebug() << QString("Starting call with %1").arg(friendNum);
    if (calls.contains(friendNum))
    {
        qWarning() << QString("Can't start call with %1, we're already in this call!").arg(friendNum);
        return false;
    }

    uint32_t videoBitrate = video ? VIDEO_DEFAULT_BITRATE : 0;
    if (!toxav_call(toxav, friendNum, AUDIO_DEFAULT_BITRATE, videoBitrate, nullptr))
        return false;

    auto call = calls.insert({friendNum, video, *this});
    call->startTimeout();
    return true;
}

bool CoreAV::cancelCall(uint32_t friendNum)
{
    if (QThread::currentThread() != coreavThread.get())
    {
        if (threadSwitchLock.test_and_set(std::memory_order_acquire))
        {
            qDebug() << "CoreAV::cancelCall: Backed off of thread-switch lock";
            return false;
        }

        bool ret;
        QMetaObject::invokeMethod(this, "cancelCall",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, ret),
                                  Q_ARG(uint32_t, friendNum));

        threadSwitchLock.clear(std::memory_order_release);
        return ret;
    }

    qDebug() << QString("Cancelling call with %1").arg(friendNum);
    if (!toxav_call_control(toxav, friendNum, TOXAV_CALL_CONTROL_CANCEL, nullptr))
    {
        qWarning() << QString("Failed to cancel call with %1").arg(friendNum);
        return false;
    }

    calls.remove(friendNum);
    emit avEnd(friendNum);
    return true;
}

void CoreAV::timeoutCall(uint32_t friendNum)
{
    // Non-blocking switch to the CoreAV thread, we really don't want to be coming
    // blocking-queued from the UI thread while we emit blocking-queued to it
    if (QThread::currentThread() != coreavThread.get())
    {
        QMetaObject::invokeMethod(this, "timeoutCall", Qt::QueuedConnection,
                                    Q_ARG(uint32_t, friendNum));
        return;
    }

    if (!cancelCall(friendNum))
    {
        qWarning() << QString("Failed to timeout call with %1").arg(friendNum);
        return;
    }
    qDebug() << "Call with friend"<<friendNum<<"timed out";
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
bool CoreAV::sendCallAudio(uint32_t callId, const int16_t *pcm, size_t samples, uint8_t chans, uint32_t rate)
{
    if (!calls.contains(callId))
        return false;

    ToxFriendCall& call = calls[callId];

    if (call.muteMic || call.inactive
            || !(call.state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_A))
    {
        return true;
    }

    // TOXAV_ERR_SEND_FRAME_SYNC means toxav failed to lock, retry 5 times in this case
    TOXAV_ERR_SEND_FRAME err;
    int retries = 0;
    do {
        if (!toxav_audio_send_frame(toxav, callId, pcm, samples, chans, rate, &err))
        {
            if (err == TOXAV_ERR_SEND_FRAME_SYNC)
            {
                ++retries;
                QThread::usleep(500);
            }
            else
            {
                qDebug() << "toxav_audio_send_frame error: "<<err;
            }
        }
    } while (err == TOXAV_ERR_SEND_FRAME_SYNC && retries < 5);
    if (err == TOXAV_ERR_SEND_FRAME_SYNC)
        qDebug() << "toxav_audio_send_frame error: Lock busy, dropping frame";

    return true;
}

void CoreAV::sendCallVideo(uint32_t callId, std::shared_ptr<VideoFrame> vframe)
{
    // We might be running in the FFmpeg thread and holding the CameraSource lock
    // So be careful not to deadlock with anything while toxav locks in toxav_video_send_frame
    if (!calls.contains(callId))
        return;

    ToxFriendCall& call = calls[callId];

    if (!call.videoEnabled || call.inactive
            || !(call.state & TOXAV_FRIEND_CALL_STATE_ACCEPTING_V))
        return;

    if (call.nullVideoBitrate)
    {
        qDebug() << "Restarting video stream to friend"<<callId;
        toxav_bit_rate_set(toxav, call.callId, -1, VIDEO_DEFAULT_BITRATE, nullptr);
        call.nullVideoBitrate = false;
    }

    ToxYUVFrame frame = vframe->toToxYUVFrame();

    if(!frame)
    {
        return;
    }

    // TOXAV_ERR_SEND_FRAME_SYNC means toxav failed to lock, retry 5 times in this case
    // We don't want to be dropping iframes because of some lock held by toxav_iterate
    TOXAV_ERR_SEND_FRAME err;
    int retries = 0;
    do {
        if (!toxav_video_send_frame(toxav, callId, frame.width, frame.height,
                                    frame.y, frame.u, frame.v, &err))
        {
            if (err == TOXAV_ERR_SEND_FRAME_SYNC)
            {
                ++retries;
                QThread::usleep(500);
            }
            else
            {
                qDebug() << "toxav_video_send_frame error: "<<err;
            }
        }
    } while (err == TOXAV_ERR_SEND_FRAME_SYNC && retries < 5);
    if (err == TOXAV_ERR_SEND_FRAME_SYNC)
        qDebug() << "toxav_video_send_frame error: Lock busy, dropping frame";
}

/**
 * @brief Toggles the mute state of the call's input (microphone).
 * @param f The friend assigned to the call
 */
void CoreAV::toggleMuteCallInput(const Friend* f)
{
    if (f && calls.contains(f->getFriendID()))
    {
        ToxCall& call = calls[f->getFriendID()];
        call.muteMic = !call.muteMic;
    }
}

/**
 * @brief Toggles the mute state of the call's output (speaker).
 * @param f The friend assigned to the call
 */
void CoreAV::toggleMuteCallOutput(const Friend* f)
{
    if (f && calls.contains(f->getFriendID()))
    {
        ToxCall& call = calls[f->getFriendID()];
        call.muteVol = !call.muteVol;
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
void CoreAV::groupCallCallback(void* tox, int group, int peer,
                               const int16_t* data, unsigned samples,
                               uint8_t channels, unsigned sample_rate,
                               void* core)
{
    Q_UNUSED(tox);

    Core* c = static_cast<Core*>(core);
    CoreAV* cav = c->getAv();

    if (!cav->groupCalls.contains(group))
    {
        return;
    }

    ToxGroupCall& call = cav->groupCalls[group];

    emit c->groupPeerAudioPlaying(group, peer);

    if (call.muteVol || call.inactive)
        return;

    Audio& audio = Audio::getInstance();
    if (!call.alSource)
        audio.subscribeOutput(call.alSource);

    audio.playAudioBuffer(call.alSource, data, samples, channels,
                          sample_rate);
}

/**
 * @brief Get a call's video source.
 * @param friendNum Id of friend in call list.
 * @return Video surface to show
 */
VideoSource *CoreAV::getVideoSourceFromCall(int friendNum)
{
    if (!calls.contains(friendNum))
    {
        qWarning() << "CoreAV::getVideoSourceFromCall: No such call, did it die before we finished answering?";
        return nullptr;
    }

    return calls[friendNum].videoSource;
}

/**
 * @brief Starts a call in an existing AV groupchat.
 * @note Call from the GUI thread.
 * @param groupId Id of group to join
 */
void CoreAV::joinGroupCall(int groupId)
{
    qDebug() << QString("Joining group call %1").arg(groupId);

    auto call = groupCalls.insert({groupId, *this});
    call->inactive = false;
}

/**
 * @brief Will not leave the group, just stop the call.
 * @note Call from the GUI thread.
 * @param groupId Id of group to leave
 */
void CoreAV::leaveGroupCall(int groupId)
{
    qDebug() << QString("Leaving group call %1").arg(groupId);

    groupCalls.remove(groupId);
}

bool CoreAV::sendGroupCallAudio(int groupId, const int16_t *pcm, size_t samples, uint8_t chans, uint32_t rate)
{
    if (!groupCalls.contains(groupId))
        return false;

    ToxGroupCall& call = groupCalls[groupId];

    if (call.inactive || call.muteMic)
        return true;

    if (toxav_group_send_audio(toxav_get_tox(toxav), groupId, pcm, samples, chans, rate) != 0)
        qDebug() << "toxav_group_send_audio error";

    return true;
}

/**
 * @brief Mutes or unmutes the group call's input (microphone).
 * @param g The group
 * @param mute True to mute, false to unmute
 */
void CoreAV::muteCallInput(const Group* g, bool mute)
{
    if (g && groupCalls.contains(g->getGroupId()))
        groupCalls[g->getGroupId()].muteMic = mute;
}

/**
 * @brief Mutes or unmutes the group call's output (speaker).
 * @param g The group
 * @param mute True to mute, false to unmute
 */
void CoreAV::muteCallOutput(const Group* g, bool mute)
{
    if (g && groupCalls.contains(g->getGroupId()))
        groupCalls[g->getGroupId()].muteVol = mute;
}

/**
 * @brief Returns the group calls input (microphone) state.
 * @param groupId The group id to check
 * @return true when muted, false otherwise
 */
bool CoreAV::isGroupCallInputMuted(const Group* g) const
{
    return g && groupCalls.contains(g->getGroupId())
            ? groupCalls[g->getGroupId()].muteMic
            : false;
}

/**
 * @brief Returns the group calls output (speaker) state.
 * @param groupId The group id to check
 * @return true when muted, false otherwise
 */
bool CoreAV::isGroupCallOutputMuted(const Group* g) const
{
    return g && groupCalls.contains(g->getGroupId())
            ? groupCalls[g->getGroupId()].muteVol
            : false;
}

/**
 * @brief Check, that group has audio or video stream
 * @param groupId Id of group to check
 * @return True for AV groups, false for text-only groups
 */
bool CoreAV::isGroupAvEnabled(int groupId) const
{
    return tox_group_get_type(Core::getInstance()->tox, groupId) == TOX_GROUPCHAT_TYPE_AV;
}

/**
 * @brief Returns the calls input (microphone) mute state.
 * @param f The friend to check
 * @return true when muted, false otherwise
 */
bool CoreAV::isCallInputMuted(const Friend* f) const
{
    return f && calls.contains(f->getFriendID())
            ? calls[f->getFriendID()].muteMic
            : false;
}

/**
 * @brief Returns the calls output (speaker) mute state.
 * @param friendId The friend to check
 * @return true when muted, false otherwise
 */
bool CoreAV::isCallOutputMuted(const Friend* f) const
{
    return f && calls.contains(f->getFriendID())
            ? calls[f->getFriendID()].muteVol
            : false;
}

/**
 * @brief Forces to regenerate each call's audio sources.
 */
void CoreAV::invalidateCallSources()
{
    for (ToxGroupCall& call : groupCalls)
    {
        call.alSource = 0;
    }

    for (ToxFriendCall& call : calls)
    {
        call.alSource = 0;
    }
}

/**
 * @brief Signal to all peers that we're not sending video anymore.
 * @note The next frame sent cancels this.
 */
void CoreAV::sendNoVideo()
{
    // We don't change the audio bitrate, but we signal that we're not sending video anymore
    qDebug() << "CoreAV: Signaling end of video sending";
    for (ToxFriendCall& call : calls)
    {
        toxav_bit_rate_set(toxav, call.callId, -1, 0, nullptr);
        call.nullVideoBitrate = true;
    }
}

void CoreAV::callCallback(ToxAV* toxav, uint32_t friendNum, bool audio, bool video, void *_self)
{
    CoreAV* self = static_cast<CoreAV*>(_self);

    // Run this slow callback asynchronously on the AV thread to avoid deadlocks with what our caller (toxcore) holds
    // Also run the code to switch to the CoreAV thread in yet another thread, in case CoreAV
    // has threadSwitchLock and wants a toxcore lock that our call stack is holding...
    if (QThread::currentThread() != self->coreavThread.get())
    {
        QtConcurrent::run([=](){
            // We assume the original caller doesn't come from the CoreAV thread here
            while (self->threadSwitchLock.test_and_set(std::memory_order_acquire))
                QThread::yieldCurrentThread(); // Shouldn't spin for long, we have priority

            QMetaObject::invokeMethod(self, "callCallback", Qt::QueuedConnection,
                                                    Q_ARG(ToxAV*, toxav), Q_ARG(uint32_t, friendNum),
                                                    Q_ARG(bool, audio), Q_ARG(bool, video), Q_ARG(void*, _self));
        });
        return;
    }

    if (self->calls.contains(friendNum))
    {
        /// Hanging up from a callback is supposed to be UB,
        /// but since currently the toxav callbacks are fired from the toxcore thread,
        /// we'll always reach this point through a non-blocking queud connection, so not in the callback.
        qWarning() << QString("Rejecting call invite from %1, we're already in that call!").arg(friendNum);
        toxav_call_control(toxav, friendNum, TOXAV_CALL_CONTROL_CANCEL, nullptr);
        return;
    }
    qDebug() << QString("Received call invite from %1").arg(friendNum);
    const auto& callIt = self->calls.insert({friendNum, video, *self});

    // We don't get a state callback when answering, so fill the state ourselves in advance
    int state = 0;
    if (audio)
        state |= TOXAV_FRIEND_CALL_STATE_SENDING_A | TOXAV_FRIEND_CALL_STATE_ACCEPTING_A;
    if (video)
        state |= TOXAV_FRIEND_CALL_STATE_SENDING_V | TOXAV_FRIEND_CALL_STATE_ACCEPTING_V;
    callIt->state = static_cast<TOXAV_FRIEND_CALL_STATE>(state);

    emit reinterpret_cast<CoreAV*>(self)->avInvite(friendNum, video);
    self->threadSwitchLock.clear(std::memory_order_release);
}

void CoreAV::stateCallback(ToxAV* toxav, uint32_t friendNum, uint32_t state, void *_self)
{
    CoreAV* self = static_cast<CoreAV*>(_self);

    // Run this slow callback asynchronously on the AV thread to avoid deadlocks with what our caller (toxcore) holds
    // Also run the code to switch to the CoreAV thread in yet another thread, in case CoreAV
    // has threadSwitchLock and wants a toxcore lock that our call stack is holding...
    if (QThread::currentThread() != self->coreavThread.get())
    {
        QtConcurrent::run([=](){
            // We assume the original caller doesn't come from the CoreAV thread here
            while (self->threadSwitchLock.test_and_set(std::memory_order_acquire))
                QThread::yieldCurrentThread(); // Shouldn't spin for long, we have priority

            QMetaObject::invokeMethod(self, "stateCallback", Qt::QueuedConnection,
                                                Q_ARG(ToxAV*, toxav), Q_ARG(uint32_t, friendNum),
                                                Q_ARG(uint32_t, state), Q_ARG(void*, _self));
        });
        return;
    }

    if (!self->calls.contains(friendNum))
    {
        qWarning() << QString("stateCallback called, but call %1 is already dead").arg(friendNum);
        self->threadSwitchLock.clear(std::memory_order_release);
        return;
    }

    ToxFriendCall& call = self->calls[friendNum];

    if (state & TOXAV_FRIEND_CALL_STATE_ERROR)
    {
        qWarning() << "Call with friend"<<friendNum<<"died of unnatural causes!";
        calls.remove(friendNum);
        emit self->avEnd(friendNum);
    }
    else if (state & TOXAV_FRIEND_CALL_STATE_FINISHED)
    {
        qDebug() << "Call with friend"<<friendNum<<"finished quietly";
        calls.remove(friendNum);
        emit self->avEnd(friendNum);
    }
    else
    {
        // If our state was null, we started the call and were still ringing
        if (!call.state && state)
        {
            call.stopTimeout();
            call.inactive = false;
            emit self->avStart(friendNum, call.videoEnabled);
        }
        else if ((call.state & TOXAV_FRIEND_CALL_STATE_SENDING_V)
                 && !(state & TOXAV_FRIEND_CALL_STATE_SENDING_V))
        {
            qDebug() << "Friend"<<friendNum<<"stopped sending video";
            if (call.videoSource)
                call.videoSource->stopSource();
        }
        else if (!(call.state & TOXAV_FRIEND_CALL_STATE_SENDING_V)
                 && (state & TOXAV_FRIEND_CALL_STATE_SENDING_V))
        {
            // Workaround toxav sometimes firing callbacks for "send last frame" -> "stop sending video"
            // out of orders (even though they were sent in order by the other end).
            // We simply stop the videoSource from emitting anything while the other end says it's not sending
            if (call.videoSource)
                call.videoSource->restartSource();
        }

        call.state = static_cast<TOXAV_FRIEND_CALL_STATE>(state);
    }
    self->threadSwitchLock.clear(std::memory_order_release);
}

void CoreAV::bitrateCallback(ToxAV* toxav, uint32_t friendNum, uint32_t arate, uint32_t vrate, void *_self)
{
    CoreAV* self = static_cast<CoreAV*>(_self);

    // Run this slow path callback asynchronously on the AV thread to avoid deadlocks
    if (QThread::currentThread() != self->coreavThread.get())
    {
        return (void)QMetaObject::invokeMethod(self, "bitrateCallback", Qt::QueuedConnection,
                                                Q_ARG(ToxAV*, toxav), Q_ARG(uint32_t, friendNum),
                                                Q_ARG(uint32_t, arate), Q_ARG(uint32_t, vrate), Q_ARG(void*, _self));
    }

    qDebug() << "Recommended bitrate with"<<friendNum<<" is now "<<arate<<"/"<<vrate<<", ignoring it";
}

void CoreAV::audioFrameCallback(ToxAV *, uint32_t friendNum, const int16_t *pcm,
                                size_t sampleCount, uint8_t channels, uint32_t samplingRate, void *_self)
{
    CoreAV* self = static_cast<CoreAV*>(_self);
    if (!self->calls.contains(friendNum))
        return;

    ToxCall& call = self->calls[friendNum];

    if (call.muteVol)
        return;

    Audio& audio = Audio::getInstance();
    if (!call.alSource)
        audio.subscribeOutput(call.alSource);

    audio.playAudioBuffer(call.alSource, pcm, sampleCount, channels, samplingRate);
}

void CoreAV::videoFrameCallback(ToxAV *, uint32_t friendNum, uint16_t w, uint16_t h,
                                const uint8_t *y, const uint8_t *u, const uint8_t *v,
                                int32_t ystride, int32_t ustride, int32_t vstride, void *)
{
    if (!calls.contains(friendNum))
        return;

    ToxFriendCall& call = calls[friendNum];
    if (!call.videoSource)
        return;

    vpx_image frame;
    frame.d_h = h;
    frame.d_w = w;
    frame.planes[0] = const_cast<uint8_t*>(y);
    frame.planes[1] = const_cast<uint8_t*>(u);
    frame.planes[2] = const_cast<uint8_t*>(v);
    frame.stride[0] = ystride;
    frame.stride[1] = ustride;
    frame.stride[2] = vstride;

    call.videoSource->pushFrame(&frame);
}
