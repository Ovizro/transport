#ifndef _INCLUDE_TRANSPORT_PROTOCOL_
#define _INCLUDE_TRANSPORT_PROTOCOL_

#include <stdint.h>
#include <vector>

namespace transport
{

class Protocol {
public:
    typedef std::vector<uint8_t> FrameType;
    
    static ssize_t pred_size(void* buf, size_t size) {
        if (buf == nullptr) return 1;
        return size;
    }

    static FrameType make_frame(void* buf, size_t size) {
        if (buf == nullptr) return FrameType();
        return FrameType((uint8_t*)buf, (uint8_t*)buf + size);
    }

    static size_t frame_size(FrameType frame) {
        return frame.size();
    }

    static void* frame_data(FrameType frame) {
        return frame.data();
    }
};

}

#endif