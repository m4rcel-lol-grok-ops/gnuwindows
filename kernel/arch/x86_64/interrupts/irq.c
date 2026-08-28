/*
 * IRQ dispatch — EOI before possible preemption on IRQ0
 */

#include <gw/irq.h>
#include <gw/pic.h>
#include <gw/timer.h>
#include <gw/kbd.h>
#include <stdint.h>

struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

void irq_dispatch(struct interrupt_frame *frame) {
    uint8_t irq = (uint8_t)(frame->vector - 32);

    switch (irq) {
    case 0:
        timer_handler();
        pic_eoi(irq);
        timer_preempt_check();
        return;
    case 1:
        kbd_irq();
        pic_eoi(irq);
        return;
    default:
        break;
    }

    pic_eoi(irq);
}

void irq_init(void) {
    pic_init();
}
