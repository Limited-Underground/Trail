#include "ot129_control_protocol.h"

#include <cassert>
#include <cstring>
#include <string>

static ot129_control_event feed(ot129_control_state &state, const std::string &text)
{
    return ot129_control_feed(
        &state, reinterpret_cast<const uint8_t *>(text.data()), text.size());
}

int main()
{
    const std::string start = OT129_CONTROL_START;
    for (std::size_t split = 0; split <= start.size(); ++split) {
        ot129_control_state state;
        ot129_control_init(&state);
        ot129_control_event first = feed(state, start.substr(0, split));
        if (split == start.size()) {
            assert(first == OT129_CONTROL_START_ACCEPTED);
            assert(feed(state, start.substr(split)) == OT129_CONTROL_NONE);
        } else {
            assert(first == OT129_CONTROL_NONE);
            assert(feed(state, start.substr(split)) == OT129_CONTROL_START_ACCEPTED);
        }
        assert(state.started);
        assert(feed(state, start) == OT129_CONTROL_ALREADY_STARTED);
    }

    ot129_control_state duplicate;
    ot129_control_init(&duplicate);
    assert(feed(duplicate, start + start) == OT129_CONTROL_START_ACCEPTED);
    assert(duplicate.started);

    const std::string repeated = start + start;
    for (std::size_t split = 0; split <= repeated.size(); ++split) {
        ot129_control_state streamed;
        ot129_control_init(&streamed);
        ot129_control_event first = feed(streamed, repeated.substr(0, split));
        ot129_control_event second = feed(streamed, repeated.substr(split));
        assert(streamed.started);
        assert(first == OT129_CONTROL_NONE ||
               first == OT129_CONTROL_START_ACCEPTED);
        assert(second == OT129_CONTROL_START_ACCEPTED ||
               second == OT129_CONTROL_ALREADY_STARTED ||
               second == OT129_CONTROL_NONE);
    }

    ot129_control_state trailing;
    ot129_control_init(&trailing);
    assert(feed(trailing, start + "junk") == OT129_CONTROL_START_ACCEPTED);
    assert(trailing.started);

    const char *invalid[] = {
        "OTCBXCTL0 START\n", "OTCBXCTL1 RUN\n", "OTCBXCTL1 STARTjunk\n",
        "OTCBXCTL1 START\r\n",
        "OTCBXCTL1 START\0\n", "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n",
    };
    for (const char *line : invalid) {
        ot129_control_state state;
        ot129_control_init(&state);
        std::size_t size = std::strcmp(line, "OTCBXCTL1 START\0\n") == 0
                               ? std::strlen("OTCBXCTL1 START") + 2U
                               : std::strlen(line);
        assert(ot129_control_feed(
                   &state, reinterpret_cast<const uint8_t *>(line), size) ==
               OT129_CONTROL_LINE_REJECTED);
        assert(!state.started);
        assert(feed(state, start) == OT129_CONTROL_START_ACCEPTED);
    }
    return 0;
}
