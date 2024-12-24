#ifndef _INCLUDED_QUEUE_
#define _INCLUDED_QUEUE_

#include <deque>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <stdexcept>

template <typename T>
class DataQueue;

class QueueException : public std::exception
{
public:
    QueueException(void* queue) : queue(queue) {}

    template <typename T>
    DataQueue<T>* GetQueue() const noexcept
    {
        return static_cast<DataQueue<T>*>(queue);
    }

private:
    void* queue;
};

class QueueCleared : public QueueException
{
public:
    QueueCleared(void* queue) : QueueException(queue) {}

    const char* what() const noexcept override
    {
        return "queue cleared";
    }
};

class QueueTimeout : public QueueException
{
public:
    QueueTimeout(void* queue) : QueueException(queue) {}

    const char* what() const noexcept override
    {
        return "queue timeout";
    }
};

typedef uint8_t queue_epoch_t;

template <typename T>
class DataQueue {
public:
    DataQueue() : m_CurrEpoch(0) {}
    DataQueue(const DataQueue&) = delete;

    ~DataQueue() { Clear(); }

    void Push(T data)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Queue.push_back(std::move(data));
        m_Cond.notify_one();
    }

    T Pop()
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        auto epoch = m_CurrEpoch;
        while (m_Queue.empty())
        {
            m_Cond.wait(lock);
            if (epoch != m_CurrEpoch) {
                throw QueueCleared(this);
            }
        }
        T data = std::move(m_Queue.front());
        m_Queue.pop_front();
        return data;
    }

    template <typename Rep = uint64_t, typename Period = std::milli>
    T Pop(const std::chrono::duration<Rep, Period> timeout)
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        auto epoch = m_CurrEpoch;
        while (m_Queue.empty())
        {
            if (m_Cond.wait_for(lock, timeout) == std::cv_status::timeout) {
                throw QueueTimeout(this);
            }
            if (epoch != m_CurrEpoch) {
                throw QueueCleared(this);
            }
        }
        T data = std::move(m_Queue.front());
        m_Queue.pop_front();
        return data;
    }

    bool Empty() noexcept
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Queue.empty();
    }

    size_t Size() noexcept
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Queue.size();
    }

    queue_epoch_t GetEpoch() noexcept
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_CurrEpoch;
    }

    bool CheckEpoch(queue_epoch_t epoch) noexcept
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_CurrEpoch == epoch;
    }

    void Clear() noexcept
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Queue.clear();
        m_CurrEpoch++;
        m_Cond.notify_all();
    }

protected:
    std::deque<T> m_Queue;
    std::mutex m_Mutex;
    std::condition_variable m_Cond;

private:
    queue_epoch_t m_CurrEpoch;
};

#endif