#ifndef INTERRUPT_UTILS_H
#define INTERRUPT_UTILS_H

static inline void interrupts_disable(uint32_t* interrupt_context)
{
    __asm__ __volatile__ ("    mrs r2, primask \r\n"
                          "    str r2, [%[interrupt_context]] \r\n"
                          "    cpsid i \r\n"
                          :
                          :[interrupt_context] "l" (interrupt_context)
                          :"memory","r2"
        );
}

static inline void interrupts_restore(uint32_t* interrupt_context)
{
    __asm__ __volatile__ ("    ldr r2, [%[interrupt_context]] \r\n"
                          "    msr primask, r2 \r\n"
                          :
                          :[interrupt_context] "l" (interrupt_context)
                          : "r2"
        );
}
#endif
