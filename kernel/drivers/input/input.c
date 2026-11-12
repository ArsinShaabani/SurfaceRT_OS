/*
 * SurfaceRT_OS - input core
 *
 * Drivers push events, the shell and (later) a touch UI consume them.  Keyboard
 * events are also translated into console characters so the serial shell stays
 * usable from a USB keyboard.
 */
#include <srtos/input.h>
#include <srtos/kernel.h>
#include <srtos/clock.h>
#include <srtos/console.h>
#include <srtos/string.h>

#define INPUT_QUEUE 64

static struct input_event queue[INPUT_QUEUE];
static volatile u32 head, tail;

static const char keymap[128] = {
    [2] = '1', [3] = '2', [4] = '3', [5] = '4', [6] = '5', [7] = '6',
    [8] = '7', [9] = '8', [10] = '9', [11] = '0', [12] = '-', [13] = '=',
    [14] = 8,   [15] = '\t', [16] = 'q', [17] = 'w', [18] = 'e', [19] = 'r',
    [20] = 't', [21] = 'y', [22] = 'u', [23] = 'i', [24] = 'o', [25] = 'p',
    [26] = '[', [27] = ']', [28] = '\r', [29] = 0,  [30] = 'a', [31] = 's',
    [32] = 'd', [33] = 'f', [34] = 'g', [35] = 'h', [36] = 'j', [37] = 'k',
    [38] = 'l', [39] = ';', [40] = '\'', [41] = '`', [43] = '\\', [44] = 'z',
    [45] = 'x', [46] = 'c', [47] = 'v', [48] = 'b', [49] = 'n', [50] = 'm',
    [51] = ',', [52] = '.', [53] = '/', [57] = ' ',
};

static const char keymap_shift[128] = {
    [2] = '!', [3] = '@', [4] = '#', [5] = '$', [6] = '%', [7] = '^',
    [8] = '&', [9] = '*', [10] = '(', [11] = ')', [12] = '_', [13] = '+',
    [26] = '{', [27] = '}', [39] = ':', [40] = '"', [41] = '~', [43] = '|',
    [51] = '<', [52] = '>', [53] = '?',
};

int input_init(void)
{
    head = tail = 0;
    pr_info("input: core ready (%d event queue)\n", INPUT_QUEUE);
    return 0;
}

static void queue_push(const struct input_event *ev)
{
    u32 next = (head + 1) % INPUT_QUEUE;
    if (next == tail) {
        return; /* drop on overflow, never block the IRQ path */
    }
    queue[head] = *ev;
    head = next;
}

int input_poll(struct input_event *ev)
{
    if (tail == head) {
        return 0;
    }
    *ev = queue[tail];
    tail = (tail + 1) % INPUT_QUEUE;
    return 1;
}

void input_report_key(u8 code, int pressed)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_KEY;
    ev.code = code;
    ev.pressed = (u8)(pressed ? 1 : 0);
    ev.timestamp_ms = (u32)clock_uptime_ms();
    queue_push(&ev);

    if (pressed && code < 128) {
        int shift = 0;
        char c = keymap[code];
        if (keymap_shift[code]) {
            shift = 1;
            c = keymap_shift[code];
        }
        (void)shift;
        if (c) {
            extern void shell_feed_char(char c);
            shell_feed_char(c);
        }
    }
}

void input_report_pointer(u16 x, u16 y, int buttons)
{
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = INPUT_POINTER_MOVE;
    ev.x = x;
    ev.y = y;
    ev.code = (u8)buttons;
    ev.timestamp_ms = (u32)clock_uptime_ms();
    queue_push(&ev);
}

void input_inject_console(void)
{
    /* reserved for the on-screen keyboard */
}
