#ifndef _INCLUDE_TRANSPORT_TTY_
#define _INCLUDE_TRANSPORT_TTY_

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <assert.h>
#include "base.hpp"

#define TRANSPORT_SERIAL_PORT_BUFFER_SIZE 1024 * 1024
// #define TRANSPORT_SERIAL_PORT_DEBUG
#ifdef COM_FRAME_DEBUG
#define TRANSPORT_SERIAL_PORT_DEBUG
#endif

namespace transport
{

template <typename P>
class SerialPortTransport : public BaseTransport<P>
{
public:
    SerialPortTransport(const std::string &path, int baudrate = 115200, size_t buffer_size = TRANSPORT_SERIAL_PORT_BUFFER_SIZE)
        : path(path), tty_id(-1), baudrate(baudrate), buffer_size(buffer_size) {}

    SerialPortTransport(int tty_id, int baudrate = 115200, size_t buffer_size = 1024)
        : tty_id(tty_id), baudrate(baudrate), buffer_size(buffer_size) {}

    ~SerialPortTransport() override
    {
        close();
    }

    void open() override
    {
        auto &logger = *logging::get_logger("transport");
        if (this->is_open)
        {
            return;
        }
        else if (this->is_closed)
        {
            logger.info("reopen serial port transport");
            this->is_open = false;
            this->is_closed = false;
        }

        if (tty_id < 0)
        {
            logger.info("open serial port %s", path.c_str());
            tty_id = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (tty_id < 0)
            {
                logger.raise_from_errno("open serial port failed");
            }
        }
        else
        {
            logger.info("use serial port %d", tty_id);
        }

        struct termios options, old;
        // deploy usart par
        memset(&options, 0, sizeof(options));
        int ret = tcgetattr(tty_id, &old);
        if (ret != 0)
        {
            logger.error("tcgetattr failed: %s", strerror(errno));
            goto end;
        }

        tcflush(tty_id, TCIOFLUSH);

        switch (baudrate)
        {
        case 9600:
            cfsetispeed(&options, B9600);
            cfsetospeed(&options, B9600);
            break;
        case 19200:
            cfsetispeed(&options, B19200);
            cfsetospeed(&options, B19200);
            break;
        case 38400:
            cfsetispeed(&options, B38400);
            cfsetospeed(&options, B38400);
            break;
        case 57600:
            cfsetispeed(&options, B57600);
            cfsetospeed(&options, B57600);
            break;
        case 115200:
            cfsetispeed(&options, B115200);
            cfsetospeed(&options, B115200);
            break;
        case 576000:
            cfsetispeed(&options, B576000);
            cfsetospeed(&options, B576000);
            break;
        case 921600:
            cfsetispeed(&options, B921600);
            cfsetospeed(&options, B921600);
            break;
        case 2000000:
            cfsetispeed(&options, B2000000);
            cfsetospeed(&options, B2000000);
            break;
        case 3000000:
            cfsetispeed(&options, B3000000);
            cfsetospeed(&options, B3000000);
            break;
        default:
            logger.error("bad baud rate %u", baudrate);
            break;
        }
        switch (1)
        {
        case 0:
            options.c_cflag &= ~PARENB;
            options.c_cflag &= ~INPCK;
            break;
        case 1:
            options.c_cflag |= (PARODD     // 使用奇校验代替偶校验
                                | PARENB); // 校验位有效
            options.c_iflag |= INPCK;      // 校验有效
            break;
        case 2:
            options.c_cflag |= PARENB;
            options.c_cflag &= ~PARODD;
            options.c_iflag |= INPCK;
            break;
        case 3:
            options.c_cflag &= ~PARENB;
            options.c_cflag &= ~CSTOPB;
            break;
        default:
            options.c_cflag &= ~PARENB;
            break;
        }

        options.c_cflag |= (CLOCAL | CREAD);
        options.c_cflag &= ~CSIZE;
        options.c_cflag &= ~CRTSCTS;
        options.c_cflag |= CS8;
        options.c_cflag &= ~CSTOPB;
        options.c_oflag = 0;
        options.c_lflag = 0;
        options.c_cc[VTIME] = 0;
        options.c_cc[VMIN] = 0;
        // 启用输出的XON/XOFF控制字符
        // Enable software flow control (XON/XOFF) for both input and output
        options.c_iflag |= (IXON | IXOFF); // Enable input and output XON/XOFF control characters
        options.c_oflag |= (IXON | IXOFF); // Enable input and output XON/XOFF control characters
        tcflush(tty_id, TCIFLUSH);

        if ((tcsetattr(tty_id, TCSANOW, &options)) != 0)
        {
            logger.error("tcsetattr failed: %s", strerror(errno));
        }

    end:
        super::open();
    }

    void close() override
    {
        if (this->is_open && !this->closed())
        {
            auto &logger = *logging::get_logger("transport");
            logger.info("close serial port %s", path.c_str());
            ::close(tty_id);
        }
        super::close();
    }

protected:
    void send_backend() override
    {
        // this->ensure_open();
        auto &logger = *logging::get_logger("transport");
        logger.debug("start serial port send backend");
        while (!this->is_closed)
        {
            auto frame_pair = this->send_que.Pop();
            auto frame = frame_pair.first;
            if (frame_pair.second && frame_pair.second->template transport<P>() != this)
            {
                logger.error("invalid token received");
                continue;
            }
            size_t remaining_size = P::frame_size(frame);
            if (remaining_size == 0) {
                continue;
            }
            logger.debug("send data %zu", remaining_size);
            size_t offset = 0;
            while (remaining_size > 0)
            {
                auto written_size = write(tty_id, static_cast<uint8_t*>(P::frame_data(frame)) + offset, remaining_size);
                if (written_size < 0)
                {
                    logger.error("write serial port failed: %s", strerror(errno));
                }
                else
                {
                    remaining_size -= written_size;
                    offset += written_size;
                }
            }
        }
    }
    
    void receive_backend() override
    {
        // this->ensure_open();
        auto &logger = *logging::get_logger("transport");
        logger.debug("start serial port receive backend");
        bool find_head = false;
        size_t min_size = P::pred_size(nullptr, 0);
        if (!min_size) min_size = 1;
        ssize_t recv_size;
        size_t pred_size = min_size * 2;    // min buffer size to keep,
        size_t offset = 0;                              // scanned data size
        size_t cached_size = 0;                         // read data size
        assert(buffer_size >= pred_size);

        uint8_t *buffer = new uint8_t[buffer_size];

        while (!this->is_closed)
        {
            recv_size = read(tty_id, ((uint8_t *)buffer) + cached_size, buffer_size - cached_size);
            if (recv_size <= 0)
            {
                if (cached_size == offset) {
                    usleep(10);
                    continue;
                }
                recv_size = 0;
            }
#ifdef TRANSPORT_SERIAL_PORT_DEBUG
            printf("receive com data (received=%zd,cached=%zu)\nbuffer: ",
                recv_size, cached_size);
            for (size_t i = 0; i < cached_size + recv_size; ++i)
            {
                printf("%02x", ((uint8_t *)buffer)[i]);
            }
            putchar('\n');
#endif
            cached_size += recv_size;

            if (!find_head)
            {
                // update offset to scan the header
                for (; offset + min_size < cached_size; ++offset)
                {
                    ssize_t pred = P::pred_size(((uint8_t *)buffer) + offset, cached_size - offset);
                    find_head = pred > 0;
                    if (find_head)
                    {
                        pred_size = pred;
                        logger.debug("find valid data (length=%zu)", pred_size);
                        if (pred_size > buffer_size)
                        {
                            logger.error("data size is too large (%zu)\n", pred_size);
                            find_head = false;
                            pred_size = min_size * 2;
                            continue;
                        }
                        break;
                    }
                }
            }

            if (find_head && cached_size >= pred_size + offset)
            {
                // all data received
                auto frame = P::make_frame((uint8_t *)buffer + offset, pred_size);
                logger.debug("receive data %zu", pred_size);
                this->recv_que.Push(std::make_pair(std::move(frame), std::make_shared<TransportToken>(this)));
                offset += pred_size; // update offset for next run
            }

            // clear the cache when the remaining length of the cache is
            if (offset && buffer_size - cached_size < pred_size)
            {
                cached_size -= offset;
                memmove(buffer, ((uint8_t *)buffer) + offset, cached_size);
                offset = 0;
            }
        }
        delete[] buffer;
    }

private:
    typedef BaseTransport<P> super;

    std::string path;
    int tty_id;
    int baudrate;
    size_t buffer_size;
};

}

#endif