#ifndef OT129_CONTROL_PROTOCOL_H
#define OT129_CONTROL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OT129_CONTROL_START "OTCBXCTL1 START\n"
#define OT129_CONTROL_READY "OTCBXCTL1 READY\n"
#define OT129_CONTROL_LINE_CAPACITY 32U

typedef enum {
    OT129_CONTROL_NONE = 0,
    OT129_CONTROL_START_ACCEPTED,
    OT129_CONTROL_LINE_REJECTED,
    OT129_CONTROL_ALREADY_STARTED,
} ot129_control_event;

typedef struct {
    uint8_t line[OT129_CONTROL_LINE_CAPACITY];
    size_t length;
    bool discarding;
    bool started;
} ot129_control_state;

void ot129_control_init(ot129_control_state *state);
ot129_control_event ot129_control_feed(
    ot129_control_state *state, const uint8_t *bytes, size_t length);

#ifdef __cplusplus
}
#endif

#endif
