#include "fb.h"
#include "arch.h"
#include <stdint.h>
#include "console.h"
#include "idt.h"
#include "pmm.h"
#include "vmm.h"
#include "mem.h"
#include "thread.h"
#include "sysinfo.h"
#include "pci.h"
#include "virtio_blk.h"
#include "fs.h"
#include "boot_gate.h"
#include "llm.h"
#include "net.h"
#include "shell.h"
#include "agent.h"
#include "ramstat.h"
#include "io.h"
#ifdef __aarch64__
#include "arch/aarch64/rpi_fb.h"
#endif

extern uint32_t multiboot_magic;
extern uint32_t multiboot_info_addr;

#define COM1 0x3F8

static void serial_init(void)
{
#ifdef __x86_64__
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
#endif
}

void kernel_main(void)
{
    serial_init();
    idt_init();
    pmm_init(multiboot_magic, multiboot_info_addr);
    vmm_init();
    vmm_apply_protections();
    kheap_init();
    thread_init();
    timer_calibrate_tsc();
    timer_init(1000);
    kprint("Hardware Timer Activo a 1000 Hz\n");
    arch_interrupts_enable();
    kprint("CPU: Interrupciones configuradas\n");
    kprint("Reloj Hardware calibrado a "); kprint_dec((uint32_t)tsc_freq_mhz);
    kprint(" MHz ("); kprint_dec((uint32_t)tsc_ticks_per_ms); kprint(" ticks/ms)\n");

    virtio_blk_init();

#ifdef __aarch64__
    if (rpi_fb_init()) {
        kprint("RPi HDMI Framebuffer inicializado (1024x768)\n");
    }
#endif

    vfs_init();

    net_init(); /* PARCHE 047: la red debe estar viva antes del canary (ping al gateway) */
    boot_gate_check();

#ifdef __x86_64__
    net_run_llm_test();
#else
    kprint("NET: Red configurada (test automatico LLM omitido en aarch64)\n");
#endif

    static char dummy_fb[64];
    show_somafetch(dummy_fb, sizeof(dummy_fb));
    ramstat_print();

    kprint("\033[1;36m----------------------------------------------------------------------\033[0m\n");
    kprint(" \033[1;37mSOMA 0.2 :: Escribe '\033[1;32msoma <mision>\033[1;37m' para el Agente o '\033[1;33mhelp\033[1;37m'.\033[0m\n");
    kprint("\033[1;36m----------------------------------------------------------------------\033[0m\n\n");

    agent_state_load();
    agent_refresh_knowledge(); /* MAYORDOMO_V2_KNOWLEDGE */
    console_history_load_from_vfs();

    static char init_dummy[128];
    char chk_init[16];
    if (vfs_read("/etc/init.sh", chk_init, sizeof(chk_init)) > 0) {
        kprint("\033[1;33m[INIT]: Ejecutando /etc/init.sh...\033[0m\n");
        dispatch_command("source /etc/init.sh", init_dummy, sizeof(init_dummy));
        kprint("\n");
    }

    kprint("\033[1;32m[SOMA ready]\033[0m Sistema interactivo listo en consola serie.\n\n");
    kprint("\033[1;36m[DESKTOP]: Iniciando Entorno Grafico SOMA por defecto...\033[0m\n");
    cmd_vsoma("", 0, 0);

    kprint("\033[1;32m[SOMA ready]\033[0m Modo texto activado.\n\n");
    shell_run();
}
