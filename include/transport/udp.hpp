#ifndef _INCLUDE_TRANSPORT_UDP_
#define _INCLUDE_TRANSPORT_UDP_

#include <memory>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include "base.hpp"

#define TRANSPORT_UDP_BUFFER_SIZE 1024 * 64

namespace transport {

class DatagramTransportToken : public TransportToken {
public:
    explicit DatagramTransportToken(_transport_base* transport, const struct sockaddr_in& addr, socklen_t addr_len)
        : TransportToken(transport), addr(addr), addr_len(addr_len) {}
    
    bool operator==(const TransportToken& other) const override
    {
        auto other_token = dynamic_cast<const DatagramTransportToken*>(&other);
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
    struct sockaddr_in addr;
    socklen_t addr_len;

    friend std::hash<DatagramTransportToken>;
    template<typename P>
    friend class DatagramTransport;
};

template<typename P>
class DatagramTransport : public BaseTransport<P> {
public:
    explicit DatagramTransport(size_t buffer_size = TRANSPORT_UDP_BUFFER_SIZE)
        : sockfd(-1), buffer_size(buffer_size)
    {
        memset(&bind_addr, 0, sizeof(bind_addr));
        memset(&connect_addr, 0, sizeof(connect_addr));
        bind_addr.sin_family = AF_INET;
        connect_addr.sin_family = AF_INET;
    }

    DatagramTransport(std::pair<const char*, int> local_addr, std::pair<const char*, int> remote_addr, size_t buffer_size = TRANSPORT_UDP_BUFFER_SIZE)
        : DatagramTransport(buffer_size)
    {
        bind_addr.sin_port = htons(local_addr.second);
        resolve_hostname(local_addr.first, bind_addr);

        connect_addr.sin_port = htons(remote_addr.second);
        resolve_hostname(remote_addr.first, connect_addr);
    }
    
    ~DatagramTransport()
    {
        close();
    }


    void open() override
    {
        auto &logger = *logging::get_logger("transport");
        if (this->is_open)
        {
            return;
        } else if (this->is_closed)
        {
            logger.info("reopen datagram transport");
            this->is_open = false;
            this->is_closed = false;
        }

        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            logger.raise_from_errno("failed to create socket");
        } else {
            logger.info("open socket fd %d", sockfd);
        }

        super::open();

        if (bind_addr.sin_port)
        {
            if (::bind(sockfd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0)
            {
                logger.raise_from_errno("failed to bind socket");
            }
            logger.info("listening on %s:%d", inet_ntoa(bind_addr.sin_addr), ntohs(bind_addr.sin_port));
        }
    }

    void close() override
    {
        if (this->is_open && !this->closed())
        {
            auto &logger = *logging::get_logger("transport");
            logger.info("close socket fd %d", sockfd);
            ::close(sockfd);
        }
        super::close();
    }

    void bind(const std::string& address, int port)
    {
        this->ensure_open();
        auto &logger = *logging::get_logger("transport");
        
        resolve_hostname(address, bind_addr);
        bind_addr.sin_port = htons(port);
        
        if (::bind(sockfd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0)
        {
            logger.raise_from_errno("failed to bind socket");
        }
        logger[logging::LogLevel::INFO] << "listening on " << address << ":" << port << std::endl;
    }

    void connect(const std::string& address, int port)
    {
        auto &logger = *logging::get_logger("transport");

        resolve_hostname(address, connect_addr);
        connect_addr.sin_port = htons(port);
        logger[logging::LogLevel::INFO] << "connecting to " << address << ":" << port << std::endl;
    }

    constexpr static std::pair<const char*, int> nulladdr = {"", 0};

protected:
    void send_backend() override
    {
        // this->ensure_open();
        auto &logger = *logging::get_logger("transport");
        logger.debug("start datagram send backend");
        while (!this->is_closed)
        {
            auto frame_pair = this->send_que.Pop();
            auto frame = frame_pair.first;
            if (!P::frame_size(frame))
                continue;
            auto token = dynamic_cast<DatagramTransportToken *>((frame_pair.second.get()));
            struct sockaddr* addr = (struct sockaddr *)((token) ? &token->addr : &connect_addr);
            socklen_t addr_len = (token) ? token->addr_len : sizeof(connect_addr);
            
            ssize_t sent_size = sendto(sockfd, P::frame_data(frame), P::frame_size(frame), 0,
                                    addr, addr_len);
            logger.debug("send data %zd", sent_size);
            if (sent_size < 0)
            {
                logger.error("udp send failed: %s", strerror(errno));
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
        auto &logger = *logging::get_logger("transport");
        logger.debug("start datagram receive backend");
        uint8_t *buffer = new uint8_t[buffer_size];
        while (!this->is_closed)
        {
            struct sockaddr_in addr;
            socklen_t addr_len = sizeof(addr);
            ssize_t recv_size = recvfrom(sockfd, buffer, buffer_size, 0,
                                        (struct sockaddr *)&addr, &addr_len);
            if (recv_size < 0)
            {
                logger.error("udp recv failed: %s", strerror(errno));
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
            this->recv_que.Push(std::make_pair(frame, std::make_shared<DatagramTransportToken>(this, addr, addr_len)));
        }

        delete[] buffer;
    }

private:
    static void resolve_hostname(const std::string& hostname, struct sockaddr_in& result)
    {
        if (hostname.empty())
        {
            result.sin_addr.s_addr = INADDR_ANY;
            return;
        }
        std::string hostname_str(hostname);
        struct hostent *he = gethostbyname(hostname_str.c_str());
        if (he == nullptr)
        {
            auto &logger = *logging::get_logger("transport");
            logger.error("failed to resolve hostname %s: %s", hostname_str.c_str(), hstrerror(h_errno));
            throw std::runtime_error("Failed to resolve hostname");
        }
        memcpy(&result.sin_addr, he->h_addr_list[0], he->h_length);
    }

    typedef BaseTransport<P> super;
    
    int sockfd;
    struct sockaddr_in bind_addr;
    struct sockaddr_in connect_addr;
    size_t buffer_size;
};

}

namespace std {
    template<>
    struct hash<transport::DatagramTransportToken> {
        size_t operator()(const transport::DatagramTransportToken &token) const
        {
            std::size_t hash1 = std::hash<transport::TransportToken>()(token);
            std::size_t hash2 = std::hash<in_addr_t>()(token.addr.sin_addr.s_addr);
            std::size_t hash3 = std::hash<in_port_t>()(token.addr.sin_port);
            hash1 ^= (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
            hash1 ^= (hash3 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
            return hash1;
        }
    };
}
#endif