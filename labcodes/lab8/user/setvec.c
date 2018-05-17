#include <stdio.h>
#include <ulib.h>
#include <syscall.h>

void handler() {
    cprintf("Triggered\n");
    exit(0);
}

int
main(void) {
    sys_setvec(255, (uintptr_t)handler);
    cprintf("Handler set\n");
    asm volatile ("int $255");
    return 0;
}

