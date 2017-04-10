/*
    Copyright © 2014-2015 by The qTox Project Contributors

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

#include "src/ipc.h"
#include "src/persistence/settings.h"
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <random>
#include <unistd.h>
#include <time.h>

/**
 * @var time_t IPC::lastEvent
 * @brief When last event was posted.
 *
 * @var time_t IPC::lastProcessed
 * @brief When processEvents() ran last time
 */

/**
 * @class IPC
 * @brief Inter-process communication
 */

IPC::IPC()
    : globalMemory{"qtox-" IPC_PROTOCOL_VERSION}
{
    qRegisterMetaType<IPCEventHandler>("IPCEventHandler");

    timer.setInterval(EVENT_TIMER_MS);
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, &IPC::processEvents);

    // The first started instance gets to manage the shared memory by taking ownership
    // Every time it processes events it updates the global shared timestamp "lastProcessed"
    // If the timestamp isn't updated, that's a timeout and someone else can take ownership
    // This is a safety measure, in case one of the clients crashes
    // If the owner exits normally, it can set the timestamp to 0 first to immediately give
    // ownership

    std::default_random_engine randEngine((std::random_device())());
    std::uniform_int_distribution<uint64_t> distribution;
    globalId = distribution(randEngine);
    qDebug() << "Our global IPC ID is " << globalId;
    if (globalMemory.create(sizeof(IPCMemory))) {
        if (globalMemory.lock()) {
            IPCMemory* mem = global();
            memset(mem, 0, sizeof(IPCMemory));
            mem->globalId = globalId;
            mem->lastProcessed = time(0);
            globalMemory.unlock();
        } else {
            qWarning() << "Couldn't lock to take ownership";
        }
    } else if (globalMemory.attach()) {
        qDebug() << "Attaching to the global shared memory";
    } else {
        qDebug() << "Failed to attach to the global shared memory, giving up";
        return; // We won't be able to do any IPC without being attached, let's get outta here
    }

    processEvents();
}

IPC::~IPC()
{
    if (isCurrentOwner()) {
        if (globalMemory.lock()) {
            global()->globalId = 0;
            globalMemory.unlock();
        }
    }
}

/**
 * @brief Returns the singleton instance.
 */
IPC& IPC::getInstance()
{
    static IPC instance;
    return instance;
}

/**
 * @brief Post IPC event.
 * @param name Name to set in IPC event.
 * @param data Data to set in IPC event (default QByteArray()).
 * @param dest Settings::getCurrentProfileId() or 0 (main instance, default).
 * @return Time the event finished.
 */
time_t IPC::postEvent(const QString& name, const QByteArray& data, uint32_t dest)
{
    QByteArray binName = name.toUtf8();
    if (binName.length() > (int32_t)sizeof(IPCEvent::name))
        return 0;

    if (data.length() > (int32_t)sizeof(IPCEvent::data))
        return 0;

    if (globalMemory.lock()) {
        IPCEvent* evt = nullptr;
        IPCMemory* mem = global();
        time_t result = 0;

        for (uint32_t i = 0; !evt && i < EVENT_QUEUE_SIZE; ++i) {
            if (mem->events[i].posted == 0)
                evt = &mem->events[i];
        }

        if (evt) {
            memset(evt, 0, sizeof(IPCEvent));
            memcpy(evt->name, binName.constData(), binName.length());
            memcpy(evt->data, data.constData(), data.length());
            mem->lastEvent = evt->posted = result = qMax(mem->lastEvent + 1, time(0));
            evt->dest = dest;
            evt->sender = getpid();
            qDebug() << "postEvent " << name << "to" << dest;
        }
        globalMemory.unlock();
        return result;
    } else
        qDebug() << "Failed to lock in postEvent()";

    return 0;
}

bool IPC::isCurrentOwner()
{
    if (globalMemory.lock()) {
        void* data = globalMemory.data();
        if (!data) {
            qWarning() << "isCurrentOwner failed to access the memory, returning false";
            globalMemory.unlock();
            return false;
        }
        bool isOwner = ((*(uint64_t*)data) == globalId);
        globalMemory.unlock();
        return isOwner;
    } else {
        qWarning() << "isCurrentOwner failed to lock, returning false";
        return false;
    }
}

/**
 * @brief Register a handler for an IPC event
 * @param handler The handler callback. Should not block for more than a second, at worst
 */
void IPC::registerEventHandler(const QString& name, IPCEventHandler handler)
{
    eventHandlers[name] = handler;
}

bool IPC::isEventAccepted(time_t time)
{
    bool result = false;
    if (globalMemory.lock()) {
        if (difftime(global()->lastProcessed, time) > 0) {
            IPCMemory* mem = global();
            for (uint32_t i = 0; i < EVENT_QUEUE_SIZE; ++i) {
                if (mem->events[i].posted == time && mem->events[i].processed) {
                    result = mem->events[i].accepted;
                    break;
                }
            }
        }
        globalMemory.unlock();
    }
    return result;
}

bool IPC::waitUntilAccepted(time_t postTime, int32_t timeout /*=-1*/)
{
    bool result = false;
    time_t start = time(0);
    forever
    {
        result = isEventAccepted(postTime);
        if (result || (timeout > 0 && difftime(time(0), start) >= timeout))
            break;

        qApp->processEvents();
        QThread::msleep(0);
    }
    return result;
}

/**
 * @brief Only called when global memory IS LOCKED.
 * @return nullptr if no evnts present, IPC event otherwise
 */
IPC::IPCEvent* IPC::fetchEvent()
{
    IPCMemory* mem = global();
    for (uint32_t i = 0; i < EVENT_QUEUE_SIZE; ++i) {
        IPCEvent* evt = &mem->events[i];

        // Garbage-collect events that were not processed in EVENT_GC_TIMEOUT
        // and events that were processed and EVENT_GC_TIMEOUT passed after
        // so sending instance has time to react to those events.
        if ((evt->processed && difftime(time(0), evt->processed) > EVENT_GC_TIMEOUT)
            || (!evt->processed && difftime(time(0), evt->posted) > EVENT_GC_TIMEOUT))
            memset(evt, 0, sizeof(IPCEvent));

        if (evt->posted && !evt->processed && evt->sender != getpid()
            && (evt->dest == Settings::getInstance().getCurrentProfileId()
                || (evt->dest == 0 && isCurrentOwner())))
            return evt;
    }

    return nullptr;
}

bool IPC::runEventHandler(IPCEventHandler handler, const QByteArray& arg)
{
    bool result = false;
    if (QThread::currentThread() == qApp->thread())
        result = handler(arg);
    else
        QMetaObject::invokeMethod(this, "runEventHandler", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, result), Q_ARG(IPCEventHandler, handler),
                                  Q_ARG(const QByteArray&, arg));

    return result;
}

void IPC::processEvents()
{
    if (globalMemory.lock()) {
        IPCMemory* mem = global();

        if (mem->globalId == globalId) {
            // We're the owner, let's process those events
            mem->lastProcessed = time(0);
        } else {
            // Only the owner processes events. But if the previous owner's dead, we can take
            // ownership now
            if (difftime(time(0), mem->lastProcessed) >= OWNERSHIP_TIMEOUT_S) {
                qDebug() << "Previous owner timed out, taking ownership" << mem->globalId << "->"
                         << globalId;
                // Ignore events that were not meant for this instance
                memset(mem, 0, sizeof(IPCMemory));
                mem->globalId = globalId;
                mem->lastProcessed = time(0);
            }
            // Non-main instance is limited to events destined for specific profile it runs
        }

        while (IPCEvent* evt = fetchEvent()) {
            QString name = QString::fromUtf8(evt->name);
            auto it = eventHandlers.find(name);
            if (it != eventHandlers.end()) {
                qDebug() << "Processing event: " << name << ":" << evt->posted << "=" << evt->accepted;
                evt->accepted = runEventHandler(it.value(), evt->data);
                if (evt->dest == 0) {
                    // Global events should be processed only by instance that accepted event.
                    // Otherwise global
                    // event would be consumed by very first instance that gets to check it.
                    if (evt->accepted)
                        evt->processed = time(0);
                } else {
                    evt->processed = time(0);
                }
            }
        }

        globalMemory.unlock();
    }
    timer.start();
}

IPC::IPCMemory* IPC::global()
{
    return static_cast<IPCMemory*>(globalMemory.data());
}
