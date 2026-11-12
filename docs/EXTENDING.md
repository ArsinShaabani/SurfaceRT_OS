# Extending the OS

## Add a service (the common case)

A service is a normal kernel thread.  Add `kernel/services/myservice.c`:

```c
#include <srtos/kernel.h>
#include <srtos/sched.h>
#include <srtos/svc.h>

static int my_init(void)
{
    pr_info("myservice: starting\n");
    return 0;
}

static int my_stop(void)
{
    return 0;
}

static struct srt_service s = {
    .name = "myservice",
    .version = "1.0",
    .init = my_init,
    .deinit = my_stop,
};

void myservice_register(void) { svc_register(&s); }
void myservice_thread_main(void *arg) { for (;;) msleep(1000); }
```

Then start a thread in `kmain()` and, optionally, call `myservice_register()` from
`svc_manager_init()` so `svc start myservice` works.

The kernel Makefile globs `services/*.c`; no build change needed.

## Add a loadable module (no kernel rebuild)

1. Create `services/mymod/mymod.c` with
   `int srt_module_init(const struct srt_host_api *api, u32 abi)`.
2. Add `mymod` to `MODULES` in `services/Makefile`.
3. `make services` produces `build/services/mymod.SVC`; the image builder copies
   it to `\SERVICES\MYMOD.SVC`.
4. On the tablet: `svc load /services/mymod.svc`, then `svc` to list it.

Modules are position independent ELF images; they may only use the host API
table.  Bump `SRT_MODULE_ABI` when you *change* an existing entry; appending new
entries at the end is backwards compatible because every call checks `abi` and
`size` first.

## Add a driver

See `docs/DRIVERS.md`.

## Add a protocol or a transport

* **New UDP protocol**: `udp_socket_open(port, cb, arg)` + `udp_send()`.  See
  `kernel/services/disp_sink.c` for a complete example.
* **New netdev**: fill a `struct net_device` (name, mac, `send` callback) and call
  `net_register_device()`; then call `net_rx(nd, frame, len)` from your receive
  path.  That is the whole integration - ARP/IP/UDP/DHCP work automatically.
  This is the shortest path to giving the tablet a usable interface before the
  88W8797 firmware is available (USB Ethernet gadget, USB Wi-Fi dongle, ...).
* **New file system**: implement `struct vfs_ops` and `vfs_mount("/mnt", root)`.

## Add a shell command

Append one entry to the table in `kernel/services/shell.c`:

```c
#define CMD(name, help) { #name, help, cmd_##name }
static int cmd_lscpu(int argc, char **argv);
…
CMD(lscpu, "print CPU information"),
```

The table lives in its own section (`.shell_cmds`, kept by the linker script) so
adding commands never changes the interpreter.

## SMP

The kernel currently runs on CPU 0 with the other three cores parked by the
firmware.  Enabling them requires:

1. releasing the cores through the PMC `PWRGATE`/`CAR` registers;
2. a per-CPU stack and a `VBAR` per core;
3. taking `spin_lock()` around every shared structure that is currently IRQ
   protected (`vfs` nodes, `pmm` bitmap);
4. per-CPU timers (the A9 private timer is already per-CPU) and IPIs for the
   scheduler.

The interrupt entry code is already re-entrant per core, so the work is in the
locking audit, not in the assembly.

## User space

The upgrade path, if isolation becomes necessary:

1. give each service a second L1 table (the MMU code already supports switching
   tables with `mmu_switch()`);
2. run services in `USR` mode with `SVC` entry points;
3. replace the host API table calls with SVC numbers - the table is already the
   only coupling between a module and the kernel, so this is a mechanical
   substitution of 20 function pointers.
