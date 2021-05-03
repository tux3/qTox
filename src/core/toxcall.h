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

#pragma once

#include "audio/iaudiocontrol.h"
#include "audio/iaudiosink.h"
#include "audio/iaudiosource.h"
#include <src/core/toxpk.h>
#include <tox/toxav.h>

#include <QMap>
#include <QMetaObject>
#include <QtGlobal>

#include <atomic>
#include <cstdint>
#include <memory>

class QTimer;
class AudioFilterer;
class CoreVideoSource;
class CoreAV;
class Group;

class ToxCall : public QObject
{
    Q_OBJECT

protected:
    ToxCall() = delete;
    ToxCall(bool VideoEnabled, CoreAV& av, IAudioControl& audio);
    ~ToxCall();

public:
    ToxCall(const ToxCall& other) = delete;
    ToxCall(ToxCall&& other) = delete;

    ToxCall& operator=(const ToxCall& other) = delete;
    ToxCall& operator=(ToxCall&& other) = delete;

    bool isActive() const;
    void setActive(bool value);

    bool getMuteVol() const;
    void setMuteVol(bool value);

    bool getMuteMic() const;
    void setMuteMic(bool value);

    bool getVideoEnabled() const;
    void setVideoEnabled(bool value);

    bool getNullVideoBitrate() const;
    void setNullVideoBitrate(bool value);

    CoreVideoSource* getVideoSource() const;

protected:
    std::atomic<bool> active{false};
    CoreAV& av;
    // audio
    IAudioControl& audio;
    std::atomic<bool> muteMic{false};
    std::atomic<bool> muteVol{false};
    // video
    CoreVideoSource* videoSource{nullptr};
    QMetaObject::Connection videoInConn;
    std::atomic<bool> videoEnabled{false};
    std::atomic<bool> nullVideoBitrate{false};
    std::unique_ptr<IAudioSource> audioSource;
};

class ToxFriendCall : public ToxCall
{
    Q_OBJECT
public:
    ToxFriendCall() = delete;
    ToxFriendCall(uint32_t friendId, bool VideoEnabled, CoreAV& av, IAudioControl& audio);
    ToxFriendCall(ToxFriendCall&& other) = delete;
    ToxFriendCall& operator=(ToxFriendCall&& other) = delete;
    ~ToxFriendCall();

    TOXAV_FRIEND_CALL_STATE getState() const;
    void setState(const TOXAV_FRIEND_CALL_STATE& value);

    void playAudioBuffer(const int16_t* data, int samples, unsigned channels, int sampleRate) const;

private slots:
    void onAudioSourceInvalidated();
    void onAudioSinkInvalidated();

private:
    QMetaObject::Connection audioSinkInvalid;
    TOXAV_FRIEND_CALL_STATE state{TOXAV_FRIEND_CALL_STATE_NONE};
    std::unique_ptr<IAudioSink> sink;
    uint32_t friendId;
};

class ToxGroupCall : public ToxCall
{
    Q_OBJECT
public:
    ToxGroupCall() = delete;
    ToxGroupCall(const Group& group, CoreAV& av, IAudioControl& audio);
    ToxGroupCall(ToxGroupCall&& other) = delete;
    ~ToxGroupCall();

    ToxGroupCall& operator=(ToxGroupCall&& other) = delete;
    void removePeer(ToxPk peerId);

    void playAudioBuffer(const ToxPk& peer, const int16_t* data, int samples, unsigned channels,
                         int sampleRate);

private:
    void addPeer(ToxPk peerId);
    bool havePeer(ToxPk peerId);
    void clearPeers();

    std::map<ToxPk, std::unique_ptr<IAudioSink>> peers;
    std::map<ToxPk, QMetaObject::Connection> sinkInvalid;
    const Group& group;

private slots:
    void onAudioSourceInvalidated();
    void onAudioSinkInvalidated(ToxPk peerId);
};
