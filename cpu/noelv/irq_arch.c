/*
 * SPDX-FileCopyrightText: 2017, 2019 Ken Rabold, JP Bonn
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/**
 * @ingroup     cpu_riscv_common
 * @{
 *
 * @file        cpu.c
 * @brief       Implementation of the CPU IRQ management for RISC-V clint/plic
 *              peripheral
 *
 * @author      Ken Rabold
 * @}
 */

#include <stdio.h>
#include <inttypes.h>

#include "macros/xtstr.h"
#include "cpu.h"
#include "context_frame.h"
#include "irq.h"
#include "irq_arch.h"
#include "panic.h"
#include "sched.h"
#include "plic.h"
#include "clic.h"
#include "architecture.h"

#include "xh3irq.h"

#include "vendor/riscv_csr.h"

/* Default state of mstatus register */
#define MSTATUS_DEFAULT (MSTATUS_MPP | MSTATUS_MPIE)

volatile int riscv_in_isr = 0;

/**
 * @brief   ISR trap vector
 */
static void trap_entry(void);

/**
 * @brief   Timer ISR
 */
void timer_isr(void);

void riscv_irq_init(void)
{
    /* Setup trap handler function */
    if (IS_ACTIVE(MODULE_PERIPH_CLIC)) {
        /* Signal CLIC usage to the core */
        write_csr(mtvec, (uintptr_t)&trap_entry | 0x03);
    }
    else {
        write_csr(mtvec, (uintptr_t)&trap_entry);
    }

    /* Clear all interrupt enables */
    write_csr(mie, 0);

    /* Initial PLIC external interrupt controller */
    if (IS_ACTIVE(MODULE_PERIPH_PLIC)) {
        plic_init();
    }
    if (IS_ACTIVE(MODULE_PERIPH_CLIC)) {
        clic_init();
    }

    /* Enable external interrupts */
    set_csr(mie, MIP_MEIP);

    /*  Set default state of mstatus */
    set_csr(mstatus, MSTATUS_DEFAULT);

    irq_enable();
}

/**
 * @brief Global trap and interrupt handler
 */
__attribute((used)) static void handle_trap(uword_t mcause)
{
    /*  Tell RIOT to set sched_context_switch_request instead of
     *  calling thread_yield(). */
    riscv_in_isr = 1;

    uword_t trap = mcause & CPU_CSR_MCAUSE_CAUSE_MSK;

    /* Check if this is an interrupt or a trap, indicated by the left most bit */
    bool is_interrupt = (mcause & MCAUSE_INT) == MCAUSE_INT;

#ifdef DEVELHELP
    if (!is_interrupt && trap != CAUSE_MACHINE_ECALL && trap != CAUSE_USER_ECALL) {
        printf("Trap: mcause=0x%" PRIxPTR " mepc=0x%" PRIxPTR " mtval=0x%" PRIxPTR "\r\n",
               (uintptr_t)mcause, (uintptr_t)read_csr(mepc), (uintptr_t)read_csr(mtval));

        if (!is_interrupt) {
            const char *error_messages[] = {
                "Instruction address misaligned",
                "Instruction access fault",
                "Illegal instruction",
                "Breakpoint",
                "Load address misaligned",
                "Load access fault",
                "Store/AMO address misaligned",
                "Store/AMO access fault",
                "Environment call from U-mode",
                "Environment call from S-mode",
                "Reserved",
                "Environment call from M-mode",
                "Instruction page fault",
                "Load page fault",
                "Reserved",
                "Store/AMO page fault",
                "Double trap",
                "Reserved",
                "Software check",
                "Hardware error"
            };

            if (trap < ARRAY_SIZE(error_messages)) {
                printf("Machine Cause Error 0x%" PRIxPTR ": %s\r\n",
                       (uintptr_t)trap, error_messages[trap]);
            }
            else {
                printf("Machine Cause Error 0x%" PRIxPTR ": Reserved / Custom\r\n",
                       (uintptr_t)trap);
            }
        }
    }
#endif

    if (is_interrupt) {
        /* Cause is an interrupt - determine type */
        switch (mcause & MCAUSE_CAUSE) {
#ifdef MODULE_PERIPH_CORETIMER
        case IRQ_M_TIMER:
            /* Handle timer interrupt */
            timer_isr();
            break;
#endif
        case IRQ_M_EXT:
            /* Handle external interrupt */
            if (IS_ACTIVE(MODULE_PERIPH_PLIC)) {
                plic_isr_handler();
            }
            if (IS_ACTIVE(MODULE_PERIPH_XH3IRQ)) {
                xh3irq_handler();
            }
            break;

        default:
            if (IS_ACTIVE(MODULE_PERIPH_CLIC)) {
                clic_isr_handler(trap);
            }
            else {
                /* Unknown interrupt */
                core_panic(PANIC_GENERAL_ERROR, "Unhandled interrupt");
            }
            break;
        }
    }
    else {
        switch (trap) {
        case CAUSE_USER_ECALL:      /* ECALL from user mode */
        case CAUSE_MACHINE_ECALL:   /* ECALL from machine mode */
        {
            /* TODO: get the ecall arguments */
            sched_context_switch_request = 1;
            /* Increment the return program counter past the ecall
             * instruction */
            uword_t return_pc = read_csr(mepc);
            write_csr(mepc, return_pc + 4);
            break;
        }
#ifdef MODULE_PERIPH_PMP
        case CAUSE_FAULT_FETCH:
            core_panic(PANIC_MEM_MANAGE, "MEM MANAGE HANDLER (fetch)");
        case CAUSE_FAULT_LOAD:
            core_panic(PANIC_MEM_MANAGE, "MEM MANAGE HANDLER (load)");
        case CAUSE_FAULT_STORE:
            core_panic(PANIC_MEM_MANAGE, "MEM MANAGE HANDLER (store)");
#endif
        default:
#ifdef DEVELHELP
            printf("Unhandled trap:\n");
            printf("  mcause: 0x%" PRIxPTR "\n", (uintptr_t)trap);
            printf("  mepc:   0x%" PRIxPTR "\n", (uintptr_t)read_csr(mepc));
            printf("  mtval:  0x%" PRIxPTR "\n", (uintptr_t)read_csr(mtval));
#endif
            /* Unknown trap */
            core_panic(PANIC_GENERAL_ERROR, "Unhandled trap");
        }
    }
    /* ISR done - no more changes to thread states */
    riscv_in_isr = 0;
}

/* Marking this as interrupt to ensure an mret at the end, provided by the
 * compiler. Aligned to 64-byte boundary as per RISC-V spec and required by some
 * of the supported platforms (gd32)*/
__attribute((aligned(64)))
static void __attribute__((interrupt)) trap_entry(void)
{
    __asm__ volatile (
        "addi sp, sp, -"XTSTR (CONTEXT_FRAME_SIZE)"          \n"

        /* Save caller-saved registers */
        "sd ra, "XTSTR (ra_OFFSET)"(sp)                      \n"
        "sd t0, "XTSTR (t0_OFFSET)"(sp)                      \n"
        "sd t1, "XTSTR (t1_OFFSET)"(sp)                      \n"
        "sd t2, "XTSTR (t2_OFFSET)"(sp)                      \n"
        "sd t3, "XTSTR (t3_OFFSET)"(sp)                      \n"
        "sd t4, "XTSTR (t4_OFFSET)"(sp)                      \n"
        "sd t5, "XTSTR (t5_OFFSET)"(sp)                      \n"
        "sd t6, "XTSTR (t6_OFFSET)"(sp)                      \n"
        "sd a0, "XTSTR (a0_OFFSET)"(sp)                      \n"
        "sd a1, "XTSTR (a1_OFFSET)"(sp)                      \n"
        "sd a2, "XTSTR (a2_OFFSET)"(sp)                      \n"
        "sd a3, "XTSTR (a3_OFFSET)"(sp)                      \n"
        "sd a4, "XTSTR (a4_OFFSET)"(sp)                      \n"
        "sd a5, "XTSTR (a5_OFFSET)"(sp)                      \n"
        "sd a6, "XTSTR (a6_OFFSET)"(sp)                      \n"
        "sd a7, "XTSTR (a7_OFFSET)"(sp)                      \n"

        /* Save s0 and s1 extra for the active thread and the stack ptr */
        "sd s0, "XTSTR (s0_OFFSET)"(sp)                      \n"
        "sd s1, "XTSTR (s1_OFFSET)"(sp)                      \n"

        /* Save the user stack ptr */
        "mv s0, sp                                          \n"
        /* Load exception stack ptr */
        "la sp, _sp                                         \n"

        /* Get the interrupt cause */
        "csrr a0, mcause                                    \n"

        /* Call trap handler, a0 contains mcause before, and the return value after
         * the call */
        "call handle_trap                                   \n"

        /* Load the sched_context_switch_request */
        "ld a0, sched_context_switch_request                \n"

        /* And skip the context switch if not requested */
        "beqz a0, no_sched                                  \n"

        /*  Get the previous active thread (could be NULL) */
        "ld s1, sched_active_thread                         \n"

        /* Run the scheduler */
        "call sched_run                                     \n"

        "no_sched:                                          \n"
        /* Restore the thread stack pointer and check if a new thread must be
         * scheduled */
        "mv sp, s0                                          \n"

        /* No context switch required, shortcut to restore. a0 contains the return
         * value of sched_run, or the sched_context_switch_request if the sched_run
         * was skipped */
        "beqz a0, no_switch                                 \n"

        /* Skips the rest of the save if no active thread */
        "beqz s1, null_thread                               \n"

        /* Store s2-s11 */
        "sd s2, "XTSTR (s2_OFFSET)"(sp)                      \n"
        "sd s3, "XTSTR (s3_OFFSET)"(sp)                      \n"
        "sd s4, "XTSTR (s4_OFFSET)"(sp)                      \n"
        "sd s5, "XTSTR (s5_OFFSET)"(sp)                      \n"
        "sd s6, "XTSTR (s6_OFFSET)"(sp)                      \n"
        "sd s7, "XTSTR (s7_OFFSET)"(sp)                      \n"
        "sd s8, "XTSTR (s8_OFFSET)"(sp)                      \n"
        "sd s9, "XTSTR (s9_OFFSET)"(sp)                      \n"
        "sd s10, "XTSTR (s10_OFFSET)"(sp)                    \n"
        "sd s11, "XTSTR (s11_OFFSET)"(sp)                    \n"

        /* Grab mepc to save it to the stack */
        "csrr s2, mepc                                      \n"

        /* Save return PC in stack frame */
        "sd s2, "XTSTR (pc_OFFSET)"(sp)                      \n"

        /* Save stack pointer of current thread */
        "sd sp, "XTSTR (SP_OFFSET_IN_THREAD)"(s1)            \n"

        /* Context saving done, from here on the new thread is scheduled */
        "null_thread:                                       \n"

        /*  Get the new active thread (guaranteed to be non NULL) */
        "ld s1, sched_active_thread                         \n"

        /*  Load the thread SP of scheduled thread */
        "ld sp, "XTSTR (SP_OFFSET_IN_THREAD)"(s1)            \n"

        /*  Set return PC to mepc */
        "ld a1, "XTSTR (pc_OFFSET)"(sp)                      \n"
        "csrw mepc, a1                                      \n"

        /* restore s2-s11 */
        "ld s2, "XTSTR (s2_OFFSET)"(sp)                      \n"
        "ld s3, "XTSTR (s3_OFFSET)"(sp)                      \n"
        "ld s4, "XTSTR (s4_OFFSET)"(sp)                      \n"
        "ld s5, "XTSTR (s5_OFFSET)"(sp)                      \n"
        "ld s6, "XTSTR (s6_OFFSET)"(sp)                      \n"
        "ld s7, "XTSTR (s7_OFFSET)"(sp)                      \n"
        "ld s8, "XTSTR (s8_OFFSET)"(sp)                      \n"
        "ld s9, "XTSTR (s9_OFFSET)"(sp)                      \n"
        "ld s10, "XTSTR (s10_OFFSET)"(sp)                    \n"
        "ld s11, "XTSTR (s11_OFFSET)"(sp)                    \n"

        "no_switch:                                         \n"

        /* restore the caller-saved registers */
        "ld ra, "XTSTR (ra_OFFSET)"(sp)                      \n"
        "ld t0, "XTSTR (t0_OFFSET)"(sp)                      \n"
        "ld t1, "XTSTR (t1_OFFSET)"(sp)                      \n"
        "ld t2, "XTSTR (t2_OFFSET)"(sp)                      \n"
        "ld t3, "XTSTR (t3_OFFSET)"(sp)                      \n"
        "ld t4, "XTSTR (t4_OFFSET)"(sp)                      \n"
        "ld t5, "XTSTR (t5_OFFSET)"(sp)                      \n"
        "ld t6, "XTSTR (t6_OFFSET)"(sp)                      \n"
        "ld a0, "XTSTR (a0_OFFSET)"(sp)                      \n"
        "ld a1, "XTSTR (a1_OFFSET)"(sp)                      \n"
        "ld a2, "XTSTR (a2_OFFSET)"(sp)                      \n"
        "ld a3, "XTSTR (a3_OFFSET)"(sp)                      \n"
        "ld a4, "XTSTR (a4_OFFSET)"(sp)                      \n"
        "ld a5, "XTSTR (a5_OFFSET)"(sp)                      \n"
        "ld a6, "XTSTR (a6_OFFSET)"(sp)                      \n"
        "ld a7, "XTSTR (a7_OFFSET)"(sp)                      \n"
        "ld s0, "XTSTR (s0_OFFSET)"(sp)                      \n"
        "ld s1, "XTSTR (s1_OFFSET)"(sp)                      \n"

        "addi sp, sp, "XTSTR (CONTEXT_FRAME_SIZE)"           \n"
        :
        :
        :
        );
}
