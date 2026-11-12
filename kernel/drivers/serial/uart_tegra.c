/*
 * SurfaceRT_OS - Tegra 3 UART driver (debug console + shell input)
 *
 * UART-A (0x70006000) is available on the Surface RT debug connector and is
 * left initialised by the firmware.  We reprogram it to 115200 8N1 so output
 * is deterministic, and use the RX interrupt to feed the shell.
 */
#include <srtos/types.h>
#include <srtos/kernel.h>
#include <srtos/console.h>
#include <srtos/irq.h>
#include <srtos/arch.h>
#include <srtos/string.h>
#include "tegra30.h"

#define UART_NUM 0
#define UART_BASE T30_UART_BASE(UART_NUM)
#define UART_IRQ 68 /* Tegra 3: UART-A = SPI 68 */

#define RX_BUF_SIZE 512

static u32 g_uart_clk = 2600000u;
static int g_ready;
static volatile u32 g_rx_head, g_rx_tail;
static char g_rx_buf[RX_BUF_SIZE];
static struct console g_console;

static void uart_console_write(const char *s, u32 len);
static int uart_console_read(char *buf, u32 len);

static inline void uart_write_reg(u32 off, u32 v)
{
    writel(v, UART_BASE + off);
}

static inline u32 uart_read_reg(u32 off)
{
    return readl(UART_BASE + off);
}

static void uart_set_baud(u32 baud)
{
    u32 div = (g_uart_clk + baud / 2) / baud;
    u32 lcr = uart_read_reg(UART_LCR);
    uart_write_reg(UART_LCR, lcr | UART_LCR_DLAB);
    uart_write_reg(UART_THR, div & 0xFF);           /* DLL */
    uart_write_reg(UART_IER, (div >> 8) & 0xFF);    /* DLM */
    uart_write_reg(UART_LCR, lcr & ~UART_LCR_DLAB);
}

void uart_init_early(const struct srt_bootinfo *bi)
{
    if (bi && bi->uart_clock_hz) {
        g_uart_clk = (u32)bi->uart_clock_hz;
    }
    uart_write_reg(UART_IER, 0);
    uart_write_reg(UART_LCR, UART_LCR_8N1 & ~UART_LCR_DLAB);
    uart_set_baud(115200);
    uart_write_reg(UART_FCR, UART_FCR_EN | UART_FCR_CLR_RX | UART_FCR_CLR_TX);
    uart_write_reg(UART_MCR, 0x10);
    g_ready = 1;
    g_rx_head = g_rx_tail = 0;

    /* register immediately: the very first printk of the kernel should reach
     * the serial port, not wait for platform_init() */
    g_console.name = "uart0";
    g_console.write = uart_console_write;
    g_console.read = uart_console_read;
    console_register(&g_console);
}

void uart_putc(char c)
{
    int spin = 200000;
    if (!g_ready) {
        return;
    }
    while (!(uart_read_reg(UART_LSR) & UART_LSR_THRE) && spin--) {
    }
    uart_write_reg(UART_THR, (u32)(u8)c);
}

int uart_getc_nonblock(void)
{
    if (!g_ready || !(uart_read_reg(UART_LSR) & UART_LSR_DR)) {
        return -1;
    }
    return (int)(uart_read_reg(UART_RBR) & 0xFF);
}

static void uart_console_write(const char *s, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++) {
        if (s[i] == '\n') {
            uart_putc('\r');
        }
        uart_putc(s[i]);
    }
}

static int uart_console_read(char *buf, u32 len)
{
    u32 n = 0;
    u32 flags = irq_save();
    while (g_rx_tail != g_rx_head && n < len) {
        buf[n++] = g_rx_buf[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1) % RX_BUF_SIZE;
    }
    irq_restore(flags);
    return (int)n;
}

static void uart_irq(int irq, void *arg)
{
    (void)irq;
    (void)arg;
    while (uart_read_reg(UART_LSR) & UART_LSR_DR) {
        char c = (char)(uart_read_reg(UART_RBR) & 0xFF);
        u32 next = (g_rx_head + 1) % RX_BUF_SIZE;
        if (next != g_rx_tail) {
            g_rx_buf[g_rx_head] = c;
            g_rx_head = next;
        }
    }
    (void)uart_read_reg(UART_IIR);
}

void uart_console_init(void)
{
    if (!g_ready) {
        return;
    }
    uart_write_reg(UART_IER, UART_IER_RDA);
    irq_register(UART_IRQ, uart_irq, 0);
    pr_info("uart: ttyS0 at 0x%08x, 115200 8N1, irq %d\n", UART_BASE, UART_IRQ);
}
