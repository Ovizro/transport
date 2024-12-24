#include "transport/unix_udp.hpp"
#include "transport/protocol.hpp"
#include "c_testcase.h"

using namespace transport;

TEST_CASE(test_init) {
    UnixDatagramTransport<Protocol> transport;
    transport.open();
    END_TEST;
}

TEST_CASE(test_send_recv) {
    UnixDatagramTransport<Protocol> transport_server("/tmp/vxup_test.sock", "");
    UnixDatagramTransport<Protocol> transport_client("/tmp/vxup_test1.sock", "/tmp/vxup_test.sock");
    transport_server.open();
    transport_client.open();
    transport_client.send(std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04});

    auto [frame, token] = transport_server.receive(std::chrono::seconds(3));
    assert_eq(frame.size(), 4);
    assert(token);
    assert_eq(frame[0], 0x01);
    assert_eq(frame[1], 0x02);
    assert_eq(frame[2], 0x03);
    assert_eq(frame[3], 0x04);

    transport_server.send(std::vector<uint8_t>{0x04, 0x03, 0x02, 0x01}, token);
    auto [frame2, token2] = transport_client.receive(std::chrono::seconds(3));
    assert_eq(frame2.size(), 4);
    assert(token2);
    assert_eq(frame2[0], 0x04);
    assert_eq(frame2[1], 0x03);
    assert_eq(frame2[2], 0x02);
    assert_eq(frame2[3], 0x01);
    END_TEST;
    END_TEST;
}
