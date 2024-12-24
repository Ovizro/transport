#ifndef _INCLUDE_TRANSPORT_UNIX_UDP_
#define _INCLUDE_TRANSPORT_UNIX_UDP_

#include <memory>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <sys/socket.h>
#include "base.hpp"

#define TRANSPORT_UDP_BUFFER_SIZE 1024

namespace transport {

class UnixDatagramTransportToken : public TransportToken {
public:
    explicit UnixDatagramTransportToken(_transport_base* transport, const struct sockaddr_un& addr, socklen_t addr_len)
        : TransportToken(transport), addr(addr), addr_len(addr_len) {}

    bool operator==(const TransportToken& other) const override
    {
        auto other_token = dynamic_cast<const UnixDatagramTransportToken*>(&other);
        if (!other_token)
        {
            return false;
        }
        if (transport_ != other_token->transport_ || addr_len != other_token->addr_len)
        {
            return false;
        }
        return memcmp(&addr, &other_token->addr, addr_len) == 0;
    }

protected:
    struct sockaddr_un addr;
    socklen_t addr_len;

    friend std::hash<UnixDatagramTransportToken>;
    template <typename P>
    friend class UnixDatagramTransport;
};

template <typename P>
class UnixDatagramTransport : public BaseTransport<P> {
public:
    explicit UnixDatagramTransport(size_t buffer_size = TRANSPORT_UDP_BUFFER_SIZE)
        : sockfd(-1), buffer_size(buffer_size)
    {
        memset(&bind_addr, 0, sizeof(bind_addr));
        memset(&connect_addr, 0, sizeof(connect_addr));
        bind_addr.sun_family = AF_UNIX;
        connect_addr.sun_family = AF_UNIX;
    }
    UnixDatagramTransport(const std::string& local_addr, const std::string& remote_addr, size_t buffer_size = TRANSPORT_UDP_BUFFER_SIZE)
        : UnixDatagramTransport(buffer_size)
    {
        set_sock_path(local_addr, bind_addr);
        set_sock_path(remote_addr, connect_addr);
    }

    ~UnixDatagramTransport()
    {
        close();
    }

    void open() override
    {
        auto& logger = *logging::get_logger("transport");
        if (this->is_open)
        {
            return;
        }
        else if (this->is_closed)
        {
            logger.info("reopen datagram transport");
            this->is_open = false;
            this->is_closed = false;
        }

        sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            logger.raise_from_errno("failed to create socket");
        } else {
            logger.info("open socket fd %d", sockfd);
        }

        super::open();

        if (*bind_addr.sun_path)
        {
            _bind();
        }
    }

    void close() override
    {
        if (this->is_open && !this->closed())
        {
            auto &logger = *logging::get_logger("transport");
            logger.info("close socket fd %d", sockfd);
            ::close(sockfd);
            if (*bind_addr.sun_path)
                unlink(bind_addr.sun_path);
        }
        super::close();
    }

    void bind(const std::string& address)
    {
        this->ensure_open();
        set_sock_path(address, bind_addr);
        _bind();
    }

    void connect(const std::string& address)
    {
        set_sock_path(address, connect_addr);
        auto& logger = *logging::get_logger("transport");
        logger[logging::LogLevel::INFO] << "connecting to " << address << std::endl;
    }

protected:
    void send_backend() override
    {
        // this->ensure_open();
        auto& logger = *logging::get_logger("transport");
        logger.debug("start datagram send backend");
        while (!this->is_closed)
        {
            auto frame_pair = this->send_que.Pop();
            auto frame = frame_pair.first;
            if (!P::frame_size(frame))
                continue;
            auto token = dynamic_cast<UnixDatagramTransportToken *>((frame_pair.second.get()));
            struct sockaddr* addr = (struct sockaddr *)((token) ? &token->addr : &connect_addr);
            socklen_t addr_len = (token) ? token->addr_len : sizeof(connect_addr);
            
            ssize_t sent_size = sendto(sockfd, P::frame_data(frame), P::frame_size(frame), 0,
                                    addr, addr_len);
            logger.debug("send data %zd", sent_size);
            if (sent_size < 0)
            {
                logger.error("unix udp send failed: %s", strerror(errno));
            }
            else if ((size_t)sent_size < P::frame_size(frame))
            {
                logger.warn("sendto failed, only %zd bytes sent", sent_size);
            }
        }
    }

    void receive_backend() override
    {
        // this->ensure_open();
        auto& logger = *logging::get_logger("transport");
        logger.debug("start datagram receive backend");
        uint8_t *buffer = new uint8_t[buffer_size];
        while (!this->is_closed)
        {
            struct sockaddr_un addr;
            socklen_t addr_len = sizeof(addr);
            ssize_t recv_size = recvfrom(sockfd, buffer, buffer_size, 0,
                                        (struct sockaddr *)&addr, &addr_len);
            if (recv_size < 0)
            {
                logger.error("unix udp recv failed: %s", strerror(errno));
                continue;
            }
            logger.debug("receive data %zd", recv_size);
            ssize_t pred_size = P::pred_size(buffer, recv_size);
            if (pred_size < 0)
            {
                logger.error("invalid frame received");
                continue;
            }
            auto frame = P::make_frame(buffer, recv_size);
            this->recv_que.Push(std::make_pair(frame, std::make_shared<UnixDatagramTransportToken>(this, addr, addr_len)));
        }
        delete[] buffer;
    }

private:
    static void set_sock_path(const std::string& path, struct sockaddr_un& result)
    {
        if (path.size() + 1 >= sizeof(result.sun_path))
        {
            auto& logger = *logging::get_logger("transport");
            logger.fatal("socket path too long");
            throw std::runtime_error("socket path too long");
        }
        strncpy(result.sun_path, path.data(), path.size());
        result.sun_path[path.size()] = '\0';
    }

    void _bind()
    {
        auto& logger = *logging::get_logger("transport");
        unlink(bind_addr.sun_path);
        if (::bind(sockfd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0)
        {
            logger.raise_from_errno("failed to bind socket");
        }
        logger.info("listening on %s", bind_addr.sun_path);
    }

    typedef BaseTransport<P> super;
    
    int sockfd;
    struct sockaddr_un bind_addr;
    struct sockaddr_un connect_addr;
    size_t buffer_size;
};

}

namespace std {
    template<>
    struct hash<transport::UnixDatagramTransportToken> {
        size_t operator()(const transport::UnixDatagramTransportToken &token) const
        {
            std::size_t hash1 = std::hash<transport::TransportToken>()(token);
            std::size_t hash2 = std::hash<std::string>()(token.addr.sun_path);
            hash1 ^= (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
            return hash1;
        }
    };
}
#endif