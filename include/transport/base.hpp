#ifndef _INCLUDE_TRANSPORT_BASE_
#define _INCLUDE_TRANSPORT_BASE_

#include <stdint.h>
#include <chrono>
#include <memory>
#include <thread>
#include <functional>
#include "dataqueue.hpp"
#include "logging/logger.hpp"
#include "protocol.hpp"

#define TRANSPORT_MAX_RETRY 5
#define TRANSPORT_TIMEOUT 1000

namespace transport
{

template <typename P>
class BaseTransport;

class _transport_base {
public:
    _transport_base() : is_open(false), is_closed(false) {}
    _transport_base(const _transport_base&) = delete;
    virtual ~_transport_base() {
        close();
    }

    virtual void open() {
        if (is_open)
            return;
        is_open = true;
        std::thread([this] {
            try {
                send_backend();
            } catch (const QueueCleared&) {}
        }).detach();
        std::thread([this] {
            try {
                receive_backend();
            } catch (const QueueCleared&) {}
        }).detach();
    }

    virtual void close() {
        is_closed = true;
    }

    bool closed() const {
        return is_closed;
    }

protected:
    void ensure_open() {
        auto& logger = *logging::get_logger("transport");
        if (is_closed)
        {
            logger.fatal("transport closed");
            throw std::runtime_error("transport closed");
        }
        if (!is_open)
        {
            open();
        }
    }
    virtual void send_backend() = 0;
    virtual void receive_backend() = 0;

    bool is_open;
    bool is_closed;
};

class TransportToken
{
public:
    explicit TransportToken(_transport_base *transport) : transport_(transport) {}

    template <typename P>
    BaseTransport<P> *transport() const {
        return dynamic_cast<BaseTransport<P>*>(transport_);
    }
    virtual bool operator==(const TransportToken &other) const {
        return transport_ == other.transport_;
    }

protected:
    _transport_base *transport_;
    friend class _transport_base;
    friend std::hash<TransportToken>;
};

template <typename P>
class BaseTransport : public _transport_base
{
public:
    typedef P Protocol;
    typedef typename P::FrameType FrameType;

    ~BaseTransport() override
    {
        close();
    }

    template <typename Rep = uint64_t, typename Period = std::milli>
    inline void send(typename P::FrameType frame, std::shared_ptr<TransportToken> token = nullptr)
    {
        ensure_open();
        send_que.Push(std::make_pair(frame, token));
    }
    template <typename Rep = uint64_t, typename Period = std::milli>
    inline std::pair<typename P::FrameType, std::shared_ptr<TransportToken>> receive(std::chrono::duration<Rep, Period> dur = std::chrono::milliseconds(0))
    {
        ensure_open();
        DataPair frame_pair;
        if (!dur.count())
            frame_pair = recv_que.Pop();
        else
            frame_pair = recv_que.Pop(dur);
        return frame_pair;
    }
    template <typename Rep = uint64_t, typename Period = std::milli>
    inline typename P::FrameType request(typename P::FrameType frame, int max_retry = TRANSPORT_MAX_RETRY, std::chrono::duration<Rep, Period> dur = std::chrono::milliseconds(TRANSPORT_TIMEOUT))
    {
        ensure_open();
        while (max_retry--)
        {
            send_que.Push(std::make_pair(frame, nullptr));
            DataPair frame_pair = recv_que.Pop(dur);
            if (frame_pair.first) {
                return frame_pair.first;
            }
            auto& logger = *logging::get_logger("transport");
            logger.warn("request timeout, retrying...");
        }
        return nullptr;
    }

    void close() override {
        is_closed = true;
        recv_que.Clear();
        send_que.Clear();
    }

    typedef std::pair<typename P::FrameType, std::shared_ptr<TransportToken>> DataPair;

protected:
    DataQueue<DataPair> send_que;
    DataQueue<DataPair> recv_que;
};

}

namespace std {
    template <>
    struct hash<transport::TransportToken>
    {
        size_t operator()(const transport::TransportToken &token) const
        {
            return std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(token.transport_));
        }
    };
}
#endif