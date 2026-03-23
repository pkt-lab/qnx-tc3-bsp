/*
 * Minimal bare-metal AArch64 guest for qvm
 * Prints "Hello from VM guest" to PL011 UART at 0x1c090000
 */
    .text
    .align 2
    .global _start

_start:
    ldr     x0, =0x1c090000    // PL011 UARTDR (matches vdev pl011 loc)
    adr     x1, msg
.loop:
    ldrb    w2, [x1], #1
    cbz     w2, .done
    str     w2, [x0]           // write char to UARTDR
    b       .loop
.done:
    wfi
    b       .done

msg:
    .asciz  "Hello from VM guest!\r\n"
