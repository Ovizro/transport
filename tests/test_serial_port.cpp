#include <pty.h>
#include <fcntl.h>
#include <thread>
#include "c_testcase.h"
#include "transport/serial_port.hpp"
#include "transport/protocol.hpp"

using namespace transport;

int master_fd = -1;
int slave_fd = -1;

const int timeout = 3;


SETUP {
    if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) < 0) {
        throw std::runtime_error("openpty failed");
    }
    fcntl(master_fd, F_SETFL, fcntl(master_fd, F_GETFL) | O_NONBLOCK);
    fcntl(slave_fd, F_SETFL, fcntl(slave_fd, F_GETFL) | O_NONBLOCK);
    return 0;   
}

TEARDOWN {
    close(master_fd);
    close(slave_fd);
    master_fd = -1;
    slave_fd = -1;
    return 0;
}

TEST_CASE(test_pty) {
    char buffer[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    ssize_t ret;
    ret = write(slave_fd, buffer, 10);
    assert_eq(ret, 10);
    ret = read(master_fd, buffer, 10);
    assert_eq(ret, 10);
    for (int i = 0; i < 10; i++) {
        assert_eq(buffer[i], i);
    }
    END_TEST;
}

TEST_CASE(test_serial_port) {
    SerialPortTransport<Protocol> t1(master_fd);
    SerialPortTransport<Protocol> t2(slave_fd);
    
    assert(!t1.closed());
    assert(!t2.closed());
    std::pair<Protocol::FrameType, std::shared_ptr<TransportToken>> data_pair;
    std::thread ([&] {
        t2.open();
        t2.send(std::vector<uint8_t>(10, 2));
    }).detach();
    t1.open();
    data_pair = t1.receive(std::chrono::seconds(timeout));
    assert_eq(data_pair.first.size(), 10);
    assert_eq(data_pair.first[0], 2);
    assert(data_pair.second);
    assert_eq(data_pair.second->transport<Protocol>(), &t1);
    t1.close();
    assert(t1.closed());
    END_TEST;
}
