/* SurfaceRT_OS - input events */
#ifndef SRTOS_INPUT_H
#define SRTOS_INPUT_H

#include <srtos/types.h>

enum input_type {
    INPUT_KEY = 1,
    INPUT_POINTER_MOVE,
    INPUT_POINTER_BUTTON,
    INPUT_TOUCH,
};

struct input_event {
    u8 type;
    u8 code;
    u8 pressed;
    u16 x, y;
    u32 timestamp_ms;
};

int input_init(void);
void input_report_key(u8 code, int pressed);
void input_report_pointer(u16 x, u16 y, int buttons);
int input_poll(struct input_event *ev);
void input_inject_console(void);

#endif
