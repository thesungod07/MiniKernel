#include <stdio.h>
#include <string.h>
#include "kernel.h"

static int post_ram_test(cpu_t *c)
{
    static const uint32_t patterns[2] = { 0xA5A5A5A5u, 0x5A5A5A5Au };

    for (int p = 0; p < 2; p++)
    {
        for (int i = 0; i < RAM_WORDS; i++)
        {
            c->ram[i] = patterns[p];
        }

        for (int i = 0; i < RAM_WORDS; i++)
        {
            if (c->ram[i] != patterns[p])
            {
                return -1;
            }
        }
    }

    memset(c->ram, 0, sizeof c->ram);
    return 0;
}

int boot_system(cpu_t *c, const uint32_t *image, size_t n)
{
    memset(&ks, 0, sizeof(ks));

    printf("POST - \n");
    int result = post_ram_test(c);

    if (result == -1)
    {
        printf("POST Failed!\n");
        return -1;
    }

    if (USER_BASE + n > RAM_WORDS)
    {
        printf("Bootloader Error: Program exceeds RAM bounds!\n");
        return -1;
    }

    printf("Loading Bootloader....\n");

    memcpy(&c->ram[USER_BASE], image, n*sizeof(uint32_t));

    printf("Bootloader: Located %lu words at address %u\n", n, USER_BASE);

    printf("Kernel: Installing interrupt handlers.....\n");
    kernel_install_handlers(c);

    c->timer_period = TIMER_PERIOD;
    c->pc = USER_BASE;
    c->mode = MODE_USER;
    c->int_enabled = true;
    
    return 0;
}
