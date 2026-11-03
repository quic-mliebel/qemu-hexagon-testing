/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "mmu.h"

#define GSR_CFI (1 << 28)
#define CCR_GEE (1 << 26)
#define CAUSE_STACK_LIMIT 0x27

#define READ_REG(reg) ({ \
    uint32_t value; \
    asm volatile("%0 = " #reg : "=r"(value) :: "memory"); \
    value; \
})
#define WRITE_REG(reg, value) \
    asm volatile(#reg " = %0" :: "r"(value) : "memory")

extern char guest_vectors[];

/* Return to monitor mode after trap1 #2 from the guest. */
void monitor_return(uint32_t ssr)
{
    SET_FIELD(ssr, SSR_UM, 0);
    SET_FIELD(ssr, SSR_GM, 0);
    setssr(ssr);
}
MY_EVENT_HANDLE(my_event_handle_trap1, monitor_return)
DEFAULT_EVENT_HANDLE(my_event_handle_error,     HANDLE_ERROR_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_nmi,       HANDLE_NMI_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissrw, HANDLE_TLBMISSRW_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_tlbmissx,  HANDLE_TLBMISSX_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_reset,     HANDLE_RESET_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_rsvd,      HANDLE_RSVD_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_trap0,     HANDLE_TRAP0_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_int,       HANDLE_INT_OFFSET)
DEFAULT_EVENT_HANDLE(my_event_handle_fperror,   HANDLE_FPERROR_OFFSET)

/*
 * Take a stack-limit fault in guest user mode with CFI active. The guest
 * kernel must get BADVA0 in GBADVA and SSR.CFI in GSR.CFI.
 */
static void test_guest_event(void)
{
    uint32_t sp = READ_REG(sp);

    WRITE_REG(s11, (uint32_t)guest_vectors); /* GEVB */
    WRITE_REG(framelimit, sp);
    asm volatile("r0 = ssr\n"
                 "r0 = setbit(r0, #16)\n" /* UM */
                 "ssr = r0\n"
                 "r0 = ##1f\n"
                 ".word 0x5200c000\n"     /* jumpr_rb r0: CFI active */
                 "1:\n"
                 ".word 0xa09de000\n"     /* allocframe_rb(r29, #0):raw */
                 ".global guest_event_done\n"
                 "guest_event_done:\n"
                 ::: "r0", "memory");
    WRITE_REG(framelimit, 0);
    check32(READ_REG(g1) & (GSR_CFI | 0xff), GSR_CFI | CAUSE_STACK_LIMIT);
    check32(READ_REG(g3), sp - 8);
}

int main(void)
{
    install_my_event_vectors();
    WRITE_REG(ccr, READ_REG(ccr) | CCR_GEE);
    test_guest_event();
    puts(err ? "FAIL" : "PASS");
    return err;
}
