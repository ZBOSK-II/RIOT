/*
 * SPDX-FileCopyrightText: 2017, 2019 JP Bonn, Ken Rabold
 * SPDX-FileCopyrightText: 2026 Matvii Ivashchenko
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#pragma once

/**
 * @ingroup     cpu_riscv_common
 * @{
 *
 * @file
 * @brief       Thread context frame stored on stack.
 *
 * @author      JP Bonn
 */

#if !defined(__ASSEMBLER__)
#include <stdint.h>
#endif /* __ASSEMBLER__ */

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__ASSEMBLER__)

/**
 * @brief   Stores the registers and PC for a context switch.
 *
 * This also defines context_switch_frame offsets for assembly language.  The
 * structure is sized to maintain 16 byte stack alignment per the ABI.
 * https://github.com/riscv/riscv-elf-psabi-doc
 *
 */
struct context_switch_frame {
    /* Callee saved registers */
    uint64_t s0;                    /**< s0 register */
    uint64_t s1;                    /**< s1 register */
    uint64_t s2;                    /**< s2 register */
    uint64_t s3;                    /**< s3 register */
    uint64_t s4;                    /**< s4 register */
    uint64_t s5;                    /**< s5 register */
    uint64_t s6;                    /**< s6 register */
    uint64_t s7;                    /**< s7 register */
    uint64_t s8;                    /**< s8 register */
    uint64_t s9;                    /**< s9 register */
    uint64_t s10;                   /**< s10 register */
    uint64_t s11;                   /**< s11 register */
    /* Caller saved registers */
    uint64_t ra;                    /**< ra register */
    uint64_t t0;                    /**< t0 register */
    uint64_t t1;                    /**< t1 register */
    uint64_t t2;                    /**< t2 register */
    uint64_t t3;                    /**< t3 register */
    uint64_t t4;                    /**< t4 register */
    uint64_t t5;                    /**< t5 register */
    uint64_t t6;                    /**< t6 register */
    uint64_t a0;                    /**< a0 register */
    uint64_t a1;                    /**< a1 register */
    uint64_t a2;                    /**< a2 register */
    uint64_t a3;                    /**< a3 register */
    uint64_t a4;                    /**< a4 register */
    uint64_t a5;                    /**< a5 register */
    uint64_t a6;                    /**< a6 register */
    uint64_t a7;                    /**< a7 register */
    /* Saved PC for return from ISR */
    uint64_t pc;                    /**< program counter */
    uint64_t pad[3];                /**< padding to maintain 16 byte alignment */
};

#endif /* __ASSEMBLER__ */

/**
 * @name Register offsets
 * @{
 */
/* These values are checked for correctness in context_frame.c */
#define s0_OFFSET     0
#define s1_OFFSET     8
#define s2_OFFSET     16
#define s3_OFFSET     24
#define s4_OFFSET     32
#define s5_OFFSET     40
#define s6_OFFSET     48
#define s7_OFFSET     56
#define s8_OFFSET     64
#define s9_OFFSET     72
#define s10_OFFSET    80
#define s11_OFFSET    88
#define ra_OFFSET     96
#define t0_OFFSET     104
#define t1_OFFSET     112
#define t2_OFFSET     120
#define t3_OFFSET     128
#define t4_OFFSET     136
#define t5_OFFSET     144
#define t6_OFFSET     152
#define a0_OFFSET     160
#define a1_OFFSET     168
#define a2_OFFSET     176
#define a3_OFFSET     184
#define a4_OFFSET     192
#define a5_OFFSET     200
#define a6_OFFSET     208
#define a7_OFFSET     216
#define pc_OFFSET     224
#define pad_OFFSET    232
/** @} */

/**
 * @brief Size of context switch frame
 */
#define CONTEXT_FRAME_SIZE (pad_OFFSET + 24)

/**
 * @brief Offset of stack pointer in struct _thread
 */
#define SP_OFFSET_IN_THREAD 0

#ifdef __cplusplus
}
#endif

/** @} */
