#include "transport/base.hpp"
#include "transport/protocol.hpp"
#include "c_testcase.h"

using namespace transport;

class TestTransport: public BaseTransport<Protocol> {
protected:
    void send_backend() override {
        while (!is_closed) {
            DataPair pair = send_que.Pop();
            recv_que.Push(std::move(pair));
        }
    }

    void receive_backend() override {}
};

const int timeout = 3;

TEST_CASE(test_init) {
    TestTransport t;
    END_TEST;
}

TEST_CASE(test_transport) {
    TestTransport t;
    assert(!t.closed());

    t.open();
    t.send(std::vector<uint8_t>(10, 1));
    auto data_pair = t.receive(std::chrono::seconds(timeout));
    assert_eq(data_pair.first.size(), 10);
    assert_eq(data_pair.first[1], 1);
    assert(!data_pair.second);
    
    t.close();
    assert(t.closed());
    END_TEST;
}
