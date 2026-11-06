.syntax unified
.thumb
.cpu cortex-m4
.section .text
.global dispatch
.thumb_func
dispatch:
 ldr r0, =0x7e000
 ldr r3, =0x7f000
gate:
 ldr r1, [r0], #4
 adds r1, #1
 bne maintenance
 cmp r0, r3
 blo gate
 /*607: all torn/active clean-radio marker states resume setup, even on POR. */
 ldr r0, =0xde000
 ldr r3, =0xdf000
clean_gate607:
 ldr r1, [r0], #4
 adds r1, #1
 bne radio
 cmp r0, r3
 blo clean_gate607
 ldr r0, =0x40000520
 ldr r1, [r0]
 cmp r1, #0xa4
 beq radio
 cmp r1, #0xa5
 beq radio
 /* Standard has an erased request. A clean flash requests initialization;
  * malformed or partial records never admit a musical runtime. */
 ldr r0, =0x7c000
 ldm r0, {r1-r5}
 ands r1, r2
 ands r1, r3
 ands r1, r4
 ands r1, r5
 adds r1, #1
 beq fresh_done
 ldr r1, [r0]
 ldr r2, =0x39524653
 cmp r1, r2
 bne maintenance
 ldr r2, [r0, #4]
 cmp r2, #1
 bne maintenance
 ldr r3, [r0, #8]
 ldr r2, =7733248
 cmp r3, r2
 bne maintenance
 eors r1, r3
 eor r1, r1, #1
 mvns r1, r1
 ldr r2, [r0, #12]
 cmp r1, r2
 bne maintenance
 ldr r1, [r0, #16]
 cmp r1, #0
 bne maintenance
fresh_done:
 /*535: one-shot auto-start after either BIN is flashed. A torn clear retries;
  * this word only requests a wake, never formatting or recovery bypass. */
 ldr r0, =0x7c014
 ldr r1, [r0]
 cmp r1, #0
 beq wake_done
 adds r1, #1
 bne maintenance
wake_done:
 ldr r0, =0x40000520
 ldr r1, [r0]
 cmp r1, #0xa7
 beq radio_once
 cmp r1, #0xa6
 beq maintenance
 cmp r1, #0xa8
 beq maintenance
 cmp r1, #0xaa
 beq maintenance
 /* Runtime performs read-only library admission before enabling storage. */
 ldr r0, =0x20000
 ldr r2, =NORMAL_RESET
 b enter
maintenance:
 ldr r0, =0x80000
 b vector
radio_once:
 movs r1, #0
 str r1, [r0]
 dsb
radio:
 ldr r0, =0xa0000
vector:
 ldr r2, [r0, #4]
enter:
 ldr r1, =0xe000ed08
 str r0, [r1]
 dsb
 isb
 ldr r1, [r0]
 msr msp, r1
 bx r2
