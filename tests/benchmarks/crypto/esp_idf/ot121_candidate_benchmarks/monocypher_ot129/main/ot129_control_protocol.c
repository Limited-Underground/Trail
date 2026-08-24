#include "ot129_control_protocol.h"

#include <string.h>

void ot129_control_init(ot129_control_state *state)
{
    memset(state, 0, sizeof(*state));
}

ot129_control_event ot129_control_feed(
    ot129_control_state *state, const uint8_t *bytes, size_t length)
{
    ot129_control_event event = OT129_CONTROL_NONE;
    const char expected[] = OT129_CONTROL_START;

    if (state->started) {
        return length == 0U ? OT129_CONTROL_NONE : OT129_CONTROL_ALREADY_STARTED;
    }
    for (size_t i = 0; i < length; ++i) {
        uint8_t value = bytes[i];
        if (state->discarding) {
            if (value == '\n') {
                state->discarding = false;
                state->length = 0U;
                event = OT129_CONTROL_LINE_REJECTED;
            }
            continue;
        }
        if (value == '\0' || value == '\r' ||
            (value < 0x20U && value != '\n') || value > 0x7eU) {
            state->discarding = value != '\n';
            state->length = 0U;
            event = OT129_CONTROL_LINE_REJECTED;
            continue;
        }
        if (state->length >= sizeof(state->line)) {
            state->discarding = value != '\n';
            state->length = 0U;
            event = OT129_CONTROL_LINE_REJECTED;
            continue;
        }
        state->line[state->length++] = value;
        if (value == '\n') {
            if (state->length == sizeof(expected) - 1U &&
                memcmp(state->line, expected, sizeof(expected) - 1U) == 0) {
                state->length = 0U;
                state->started = true;
                return OT129_CONTROL_START_ACCEPTED;
            }
            state->length = 0U;
            event = OT129_CONTROL_LINE_REJECTED;
        }
    }
    return event;
}
