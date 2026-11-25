#include <stdio.h>
#include <string.h>
#include "kernel.h"

static int post_ram_test(cpu_t *c)
{
    static const uint32_t patterns[2] = { 0xA5A5A5A5u, 0x5A5A5A5Au };
    for (int p = 0; p < 2; p++)
    {
        for (int i = 0; i < RAM_WORDS; i++) { c->ram[i] = patterns[p]; }
        for (int i = 0; i < RAM_WORDS; i++) { if (c->ram[i] != patterns[p]) { return -1; } }
    }
    memset(c->ram, 0, sizeof c->ram);
    return 0;
}

int boot_system(cpu_t *c, const uint32_t *image, size_t n)
{
    memset(&ks, 0, sizeof ks);

    printf("[kernel] stage 1: POST\n");
    if (post_ram_test(c) < 0) { printf("[kernel] RAM test FAILED\n"); return -1; }
    printf("[kernel] RAM OK (%d words)\n", RAM_WORDS);

    printf("[kernel] stage 2: bootloader\n");
    if (USER_BASE + n > RAM_WORDS) { printf("[kernel] image too large\n"); return -1; }
    memcpy(&c->ram[USER_BASE], image, n * sizeof(uint32_t));
    printf("[kernel] loaded %zu words at 0x%04x\n", n, USER_BASE);

    printf("[kernel] stage 3: kernel init\n");
    kernel_install_handlers(c);
    c->timer_period = TIMER_PERIOD;
    c->base = USER_BASE;
    c->limit = RAM_WORDS - USER_BASE;
    c->pc = 0;
    c->mode = MODE_USER;
    c->int_enabled = true;
    printf("[kernel] handlers installed, timer armed, dropping to user mode\n");
    return 0;
}
