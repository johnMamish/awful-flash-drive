#include "samd21.h"

#include "char_buffer.h"
#include "usb_descriptors.h"

#include <stdint.h>
#include <string.h>

const volatile uint8_t *NVM_SOFTWARE_CAL_AREA = (void*)0x806020;

volatile char_buffer_t sercom3_tx_buf;
uint8_t sercom3_tx_buf_space[1024];

static volatile UsbDeviceDescriptor endpoint_descriptors[8] __attribute__((aligned(4))) = { 0 };

uint8_t ep0_out_buf[64];
uint8_t ep0_in_buf[64];


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

void SERCOM3_putx8(uint8_t x)
{
    for (int i = 0; i < 2; i++, x <<= 4) {
        uint8_t buux = ((x & 0xf0) >> 4);
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

void hexprint(uint8_t *data, int len)
{
    for (int i = 0; i < len; i++) {
        SERCOM3_putx8(data[i]);
        SERCOM3_putch(' ');
        if ((i & 0x0f) == 0x0f)
            SERCOM3_puts("\r\n");
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

    // USB clock configurations. No APBBMASK needed, USB enabled by default.
    GCLK->CLKCTRL.reg = (1 << 14) | (0 << 8) | (0x06 << 0);

    // Configure GPIOs to work with USB, PA24 -> D-; PA25 -> D+.
    PORT->Group[0].PMUX[(24 >> 1)].reg = (6 << 4) | (6 << 0);
    PORT->Group[0].PINCFG[24].reg = 1;
    PORT->Group[0].PINCFG[25].reg = 1;

    // Load PADCAL registers
    // todo: mess around by changing / forgetting these values and see what happens
    USB->DEVICE.PADCAL.bit.TRIM   = (((NVM_SOFTWARE_CAL_AREA[7] & 0b00000011) << 1) |
                                     ((NVM_SOFTWARE_CAL_AREA[6] & 0b10000000) >> 7));
    USB->DEVICE.PADCAL.bit.TRIM   = (((NVM_SOFTWARE_CAL_AREA[6] & 0b00000011) << 3) |
                                     ((NVM_SOFTWARE_CAL_AREA[5] & 0b11100000) >> 5));
    USB->DEVICE.PADCAL.bit.TRANSP = (((NVM_SOFTWARE_CAL_AREA[6] & 0b01111100) >> 2));

    // Enable USB
    USB->DEVICE.CTRLA.bit.MODE = USB_CTRLA_MODE_DEVICE;
    USB->DEVICE.CTRLB.bit.SPDCONF = USB_DEVICE_CTRLB_SPDCONF_FS;
    USB->DEVICE.CTRLA.bit.ENABLE = 1;
    USB->DEVICE.DESCADD.reg = (uint32_t)endpoint_descriptors;
    USB->DEVICE.INTENSET.bit.EORST = 1;

    // BASE ENDPOINT CONFIGURATIONS -- need to be re-run after usb reset
    // Configure endpoint 0
    USB->DEVICE.DeviceEndpoint[0].EPCFG.bit.EPTYPE0 = 1;
    USB->DEVICE.DeviceEndpoint[0].EPCFG.bit.EPTYPE1 = 1;
    USB->DEVICE.DeviceEndpoint[0].EPINTENSET.bit.RXSTP = 1;
    // </ENDPOINT CONFIGURATIONS>

    endpoint_descriptors[0].DeviceDescBank[0].ADDR.reg = (uint32_t)ep0_out_buf;
    endpoint_descriptors[0].DeviceDescBank[0].PCKSIZE.bit.SIZE = 3;

    endpoint_descriptors[0].DeviceDescBank[1].ADDR.reg = (uint32_t)ep0_in_buf;
    endpoint_descriptors[0].DeviceDescBank[1].PCKSIZE.bit.SIZE = 3;

    // Attach USB hardware
    USB->DEVICE.CTRLB.bit.DETACH = 0;
}

int main()
{
    char_buffer_init(&sercom3_tx_buf, sercom3_tx_buf_space, sizeof(sercom3_tx_buf_space));

    //
    init_hardware();

    // enable sercom interrupts in nvic
    NVIC_EnableIRQ(SERCOM3_IRQn);
    NVIC_EnableIRQ(USB_IRQn);
    asm volatile("cpsie if");

    SERCOM3_puts("=================\r\n");

    // startup PORT peripheral in power manager
    // nothing to do. on by default.

    // wait until we get a setup packet, then print its contents
    while(1) {

    }
}

const uint8_t descriptor[] =
{
    18,                                  // size of descriptor in bytes
    USB_DEVICE_DESCRIPTOR_TYPE_DEVICE,   // descriptor type
    0x10,                                // usb version
    0x02,
    0xff,                                // dev class
    0xff,                                // dev subclass
    0xff,                                // device protocol
    64,                                  // ep0 max packet size
    0x11,                                // vendor id lsb
    0xba,                                // vendor id msb
    0x0f,                                // product id
    0xf0,
    0x01,                                // device release #
    0x00,
    0x00,                                // manufacturer string idx
    0x00,                                // product string idx
    0x00,                                // serial number string idx
    0x01,                                // nconfigs
};


uint32_t fill_setup_response(volatile usb_device_request_t *req, volatile uint8_t *dest)
{
    uint32_t bytes_filled = 0;
    switch (req->request) {
        // GET_DESCRIPTOR
        case 0x06: {
            bytes_filled = (req->length > 18) ? 18 : req->length;
            memcpy((uint8_t*)dest, descriptor, bytes_filled);
            break;
        }
    }

    return bytes_filled;
}


void USB_Handler()
{
    static uint8_t addr = 0;

    // handle usb events
    if (USB->DEVICE.INTFLAG.bit.EORST) {
        SERCOM3_puts("USB reset\r\n");
        USB->DEVICE.DeviceEndpoint[0].EPCFG.bit.EPTYPE0 = 1;
        USB->DEVICE.DeviceEndpoint[0].EPINTENSET.bit.RXSTP = 1;
        USB->DEVICE.DeviceEndpoint[0].EPINTENSET.bit.TRCPT0 = 1;
        USB->DEVICE.DeviceEndpoint[0].EPINTENSET.bit.TRCPT1 = 1;
        USB->DEVICE.INTFLAG.reg = USB_DEVICE_INTFLAG_EORST;
    }

    // handle endpoint 0 events
    if (USB->DEVICE.EPINTSMRY.bit.EPINT0) {
        static usb_device_request_t request;

        // TODO: would it be nice if we had some sort of state machine keeping track of what we
        // expect next during setup transactions?
        // Check and see if a setup packet was rx'd. If it was, either fill the buffer with the
        // requested data or latch the request and prepare to recieve a command IN stage.
        if (USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.bit.RXSTP) {
            SERCOM3_puts("got SETUP, ");
            SERCOM3_putx(endpoint_descriptors[0].DeviceDescBank[0].PCKSIZE.bit.BYTE_COUNT);
            SERCOM3_puts(" bytes:\r\n");
            hexprint(ep0_out_buf, 8);
            SERCOM3_puts("\r\n");

            // save setup request
            memcpy(&request, ep0_out_buf, 8);

            if (request.request_type & (1 << 7)) {
                // SETUP for device-to-host transfer
                uint32_t bytes_to_send = fill_setup_response(&request, (void *)ep0_in_buf);
                endpoint_descriptors[0].DeviceDescBank[1].PCKSIZE.bit.BYTE_COUNT = bytes_to_send;
                USB->DEVICE.DeviceEndpoint[0].EPSTATUSSET.bit.BK1RDY = 1;
            } else {
                // handle commands without a data stage
                // NB: the only host->device command with a data stage is "SET_DESCRIPTOR"
                addr = ep0_out_buf[2];
                endpoint_descriptors[0].DeviceDescBank[1].PCKSIZE.bit.BYTE_COUNT = 0;
                USB->DEVICE.DeviceEndpoint[0].EPSTATUSSET.bit.BK1RDY = 1;

            }

            USB->DEVICE.DeviceEndpoint[0].EPSTATUSCLR.bit.BK0RDY = 1;
            // clear RXSTP bit
            USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_RXSTP;
        }


        if (USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.bit.TRCPT0) {
            SERCOM3_puts("TRCPT, ");
            SERCOM3_putx(endpoint_descriptors[0].DeviceDescBank[0].PCKSIZE.bit.BYTE_COUNT);
            SERCOM3_puts(" bytes:\r\n");
            hexprint(ep0_out_buf, 8);
            SERCOM3_puts("\r\n");

            USB->DEVICE.DeviceEndpoint[0].EPSTATUSCLR.bit.BK0RDY = 1;
            USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_TRCPT0;
        }

        if (USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.bit.TRCPT1) {
            if (request.request == 5) {
                USB->DEVICE.DADD.reg = (1 << 7) | addr;
            }
            USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_TRCPT1;
        }
    }

    SERCOM3_puts("\r\n");
}
