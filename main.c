#include "samd21.h"

#include "char_buffer.h"

#include <stdint.h>

const volatile uint8_t *NVM_SOFTWARE_CAL_AREA = (void*)0x806020;

volatile char_buffer_t sercom3_tx_buf;
uint8_t sercom3_tx_buf_space[256];

void SERCOM3_putch(char ch)
{
    asm volatile("cpsid if");
    if (SERCOM3->USART.INTENSET.reg & SERCOM_USART_INTENSET_DRE) {
        // If the txempty interrupt is already enabled, it's expecting data in the buffer
        char_buffer_putc(&sercom3_tx_buf, ch);
    } else {
        // put char in and enable interrupt
        SERCOM3->USART.INTENSET.reg = SERCOM_USART_INTENSET_DRE;
        SERCOM3->USART.DATA.reg = ch;
    }
    asm volatile("cpsie if");
}


void SERCOM3_puts(const char *s)
{
    for (; *s; s++)
        SERCOM3_putch(*s);
}


void SERCOM3_putx(uint32_t x)
{
    for (int i = 0; i < 8; i++, x <<= 4) {
        uint8_t buux = ((x & 0xf0000000) >> 28);
        if (buux <= 9) {
            buux += '0';
        } else {
            buux += ('a' - 10);
        }
        SERCOM3_putch((char)buux);
    }
}


void SERCOM3_puti(uint32_t n)
{
    char buf[33];
    int i = 0;
    do {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    } while(n);

    for (int j = (i - 1); j >= 0; j--) {
        SERCOM3_putch(buf[j]);
    }
}


void SERCOM3_Handler()
{
    uint8_t sr_start = SERCOM3->USART.INTFLAG.reg;

    // TX handling
    if ((SERCOM3->USART.INTENSET.reg & SERCOM_USART_INTENSET_DRE) &&
        (sr_start & SERCOM_USART_INTFLAG_DRE)) {
        if (char_buffer_isempty(&sercom3_tx_buf)) {
            SERCOM3->USART.INTENCLR.reg = SERCOM_USART_INTENSET_DRE;
        } else {
            uint8_t ch;
            char_buffer_getc(&sercom3_tx_buf, &ch);
            SERCOM3->USART.DATA.reg = ch;
        }
    }

    // TODO RX
}


void init_hardware()
{
    // Clock initialization. 32k xtal --> GCLK1
    GCLK->GENCTRL.reg = (GCLK_GENCTRL_GENEN |
                         GCLK_GENCTRL_SRC(GCLK_GENCTRL_SRC_XOSC32K_Val) |
                         GCLK_GENCTRL_ID(1));

    SYSCTRL->XOSC32K.bit.ONDEMAND = 0;
    SYSCTRL->XOSC32K.bit.EN32K = 1;
    SYSCTRL->XOSC32K.bit.XTALEN = 1;
    SYSCTRL->XOSC32K.bit.STARTUP = 1;
    SYSCTRL->XOSC32K.bit.ENABLE = 1;

    while (!SYSCTRL->PCLKSR.bit.XOSC32KRDY);

    // GCLK1 --> DFLL48M
    GCLK->CLKCTRL.reg = (GCLK_CLKCTRL_CLKEN |
                         GCLK_CLKCTRL_GEN(1) |
                         GCLK_CLKCTRL_ID(GCLK_CLKCTRL_ID_DFLL48_Val));

    //we may need to wait for clock domains to sync up.
    while((!(SYSCTRL->PCLKSR.reg & (1 << 4))));


    //we need to clear the dfllctrl register before we try to configure the
    //dfll or else the mcu may freeze.  Check chapter 39.1.4.5 on the

    //samd21 datasheet.
    SYSCTRL->DFLLCTRL.reg = 0;

    SYSCTRL->DFLLVAL.bit.COARSE = (NVM_SOFTWARE_CAL_AREA[7] >> 2) & 0x3f;
    SYSCTRL->DFLLVAL.bit.FINE = (NVM_SOFTWARE_CAL_AREA[8] |
                                 ((NVM_SOFTWARE_CAL_AREA[9] & 0x03) << 8));

    SYSCTRL->DFLLMUL.reg = (0 << 26) | (0 << 24) | (1465);
    SYSCTRL->DFLLCTRL.bit.ONDEMAND = 0;
    SYSCTRL->DFLLCTRL.bit.ENABLE = 1;
    SYSCTRL->DFLLCTRL.bit.MODE = 1;

    while (!(SYSCTRL->PCLKSR.bit.DFLLLCKC &&
             SYSCTRL->PCLKSR.bit.DFLLLCKF &&
             SYSCTRL->PCLKSR.bit.DFLLRDY));


    // DFLL48M --> GEN0 --> core
    //enable nvm cache and wait states, then switch over to DFLL48M for main clock
    *((uint32_t*)0x41004004) = (1 << 1);
    GCLK->GENCTRL.reg = (GCLK_GENCTRL_GENEN |
                         GCLK_GENCTRL_SRC(GCLK_GENCTRL_SRC_DFLL48M_Val) |
                         GCLK_GENCTRL_ID(0));

    // Initialize PA22 and PA23 to be SERCOM3 PAD[0,1] UART TX,RX. These are hooked to the xplained
    // built-in com port as explained in table 4.3.2 of the samd21 xplained pro user manual.
    PORT->Group[0].PMUX[(22 >> 1)].reg = (2 << 4) | (2 << 0);
    PORT->Group[0].PINCFG[22].reg = 1;
    PORT->Group[0].PINCFG[23].reg = 1;

    // Initialize sercom3 as a UART running at a 921600 baud off of the DFLL48M
    PM->APBCMASK.reg |= (0x3f << 2);
    GCLK->CLKCTRL.reg = (1 << 14) | (0 << 8) | (0x13 << 0);
    GCLK->CLKCTRL.reg = (1 << 14) | (0 << 8) | (0x17 << 0);
    SERCOM3->USART.CTRLA.reg = (1 << 30) | (1 << 20) | (1 << 13) | (1 << 2);
    SERCOM3->USART.BAUD.reg  = (4 << 13) | 6;
    SERCOM3->USART.CTRLB.reg = (1 << 17) | (1 << 16);
    SERCOM3->USART.CTRLA.reg |= (1 << 1);
    SERCOM3->USART.INTENCLR.reg = SERCOM_USART_INTENCLR_DRE;

    // Initialize USB

    // Attach USB hardware

}

int main()
{
    char_buffer_init(&sercom3_tx_buf, sercom3_tx_buf_space, sizeof(sercom3_tx_buf_space));

    //
    init_hardware();

    // enable sercom interrupts in nvic
    NVIC_EnableIRQ(SERCOM3_IRQn);
    asm volatile("cpsie if");

    // startup PORT peripheral in power manager
    // nothing to do. on by default.


    // Wait until usb is attached.

    while(1) {
        for (volatile int i = 0; i < 100000; i++);
        for (char i = 'a'; i <= 'z'; i++) {
            SERCOM3_putch(i);
        }
        SERCOM3_puti(3141592653);
    }
}
