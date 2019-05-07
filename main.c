/**
 * Copyright (C) 2019 John Mamish
 * All right reserved.
 *
 * This software was developed independently by John Mamish. Please contact me at
 * john.mamish <at> gmail dotcom to license it.
 */

#include "samd21.h"

#include "char_buffer.h"
#include "interrupt_utils.h"
#include "scsi.h"
#include "usb_descriptors.h"

#include <stdint.h>
#include <string.h>

/**
 * NB: a serial number string descriptor is REQUIRED for mass storage spec.
 *
 * USB todos:
 *   ( ) Handle status stages which are longer than the endpoint size
 *   ( ) Keep track of which status stage we are in so that we can recover from errors
 *   ( ) Could save memcpys when it comes to fixed descriptors by just pointing the USB DMA at the
 *       fixed descriptor
 */

const volatile uint8_t *NVM_SOFTWARE_CAL_AREA = (void*)0x806020;

volatile char_buffer_t sercom3_tx_buf;
uint8_t sercom3_tx_buf_space[2048];

static volatile UsbDeviceDescriptor endpoint_descriptors[8] __attribute__((aligned(4))) = { 0 };

uint8_t ep0_out_buf[64];
uint8_t ep0_in_buf[64];

uint8_t ep1_in_buf[64];
uint8_t ep2_out_buf[64];

void SERCOM3_putch(char ch)
{
    uint32_t ctx;
    //interrupts_disable(&ctx);
    asm volatile("cpsid i");
    if (SERCOM3->USART.INTENSET.reg & SERCOM_USART_INTENSET_DRE) {
        // If the txempty interrupt is already enabled, it's expecting data in the buffer
        char_buffer_putc(&sercom3_tx_buf, ch);
    } else {
        // put char in and enable interrupt
        SERCOM3->USART.INTENSET.reg = SERCOM_USART_INTENSET_DRE;
        SERCOM3->USART.DATA.reg = ch;
    }
    //interrupts_restore(&ctx);
    asm volatile ("cpsie i");
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

void hexprint(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++) {
        SERCOM3_putx8(data[i]);
        SERCOM3_putch(' ');
        if ((i & 0x0f) == 0x0f)
            SERCOM3_puts("\r\n");
    }
}


void cbw_print(const usb_mass_storage_cbw_t *cbw) {
    uint8_t *sig = (uint8_t*)&cbw->cbw_signature;
    SERCOM3_puts("  sig: "); for(int i = 0; i < 4; SERCOM3_putch(sig[i++])); SERCOM3_puts("\r\n");
    SERCOM3_puts("  tag: "); SERCOM3_putx8(cbw->cbw_tag); SERCOM3_puts("\r\n");
    SERCOM3_puts("  len: "); SERCOM3_putx8(cbw->cbw_data_transfer_length); SERCOM3_puts("\r\n");
    SERCOM3_puts("  flags: "); hexprint((uint8_t*)&cbw->cbw_flags, 1); SERCOM3_puts("\r\n");
    SERCOM3_puts("  cbwcblen: "); hexprint((uint8_t*)&cbw->cbwcb_length, 1); SERCOM3_puts("\r\n");
    hexprint(cbw->cbwcb, 16);SERCOM3_puts("\r\n");
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

    // Load Padcal Registers
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
    USB->DEVICE.DeviceEndpoint[0].EPINTENSET.bit.TRCPT0 = 1;
    USB->DEVICE.DeviceEndpoint[0].EPINTENSET.bit.TRCPT1 = 1;

    USB->DEVICE.DeviceEndpoint[1].EPCFG.bit.EPTYPE0 = 0;
    USB->DEVICE.DeviceEndpoint[1].EPCFG.bit.EPTYPE1 = 3;
    USB->DEVICE.DeviceEndpoint[1].EPINTENSET.bit.TRCPT1 = 1;
    USB->DEVICE.DeviceEndpoint[1].EPINTENSET.bit.STALL1 = 1;

    USB->DEVICE.DeviceEndpoint[2].EPCFG.bit.EPTYPE0 = 3;
    USB->DEVICE.DeviceEndpoint[2].EPCFG.bit.EPTYPE1 = 0;
    USB->DEVICE.DeviceEndpoint[2].EPINTENSET.bit.TRCPT0 = 1;
    USB->DEVICE.DeviceEndpoint[2].EPINTENSET.bit.STALL0 = 1;
    // </ENDPOINT CONFIGURATIONS>

    endpoint_descriptors[0].DeviceDescBank[0].ADDR.reg = (uint32_t)ep0_out_buf;
    endpoint_descriptors[0].DeviceDescBank[0].PCKSIZE.bit.SIZE = 3;

    endpoint_descriptors[0].DeviceDescBank[1].ADDR.reg = (uint32_t)ep0_in_buf;
    endpoint_descriptors[0].DeviceDescBank[1].PCKSIZE.bit.SIZE = 3;

    endpoint_descriptors[1].DeviceDescBank[1].ADDR.reg = (uint32_t)ep1_in_buf;
    endpoint_descriptors[1].DeviceDescBank[1].PCKSIZE.bit.SIZE = 3;

    endpoint_descriptors[2].DeviceDescBank[0].ADDR.reg = (uint32_t)ep2_out_buf;
    endpoint_descriptors[2].DeviceDescBank[0].PCKSIZE.bit.SIZE = 3;

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
    while(1) {

    }
}

const uint8_t descriptor[] =
{
    18,                                  // size of descriptor in bytes
    USB_DEVICE_DESCRIPTOR_TYPE_DEVICE,   // descriptor type
    0x10,                                // usb version
    0x02,
    0,                                   // dev class
    0,                                   // dev subclass
    0,                                   // device protocol
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


/**
 * order to flatten these descriptors is on page 253 of the usb 2.0 spec
 * it looks like the only way to get to the interface and endpoint descriptors is by requesting the
 * "full" configuration descriptor. (yes, page 267 says so)
 */
uint8_t configuration_descriptor[] =
{
    9,
    USB_DEVICE_DESCRIPTOR_TYPE_CONFIGURATION,
    32,   // wTotalLength
    0,
    1,    // bNumInterfaces
    1,    // bConfig value
          // NB: linux source comments informed me this value should start at 1.
          // USB 2.0 spec section 9.1.1.5 implies that this value must not be 0.
    0,    // string index
    0x80,
    50,   // power consumption for this configuration.

    // ================================
    9,
    USB_DEVICE_DESCRIPTOR_TYPE_INTERFACE,
    0,  // interfaceNumber
    0,  // aternateSetting
    2,  // numEndpoints
    0x08,  // interfaceClass: mass storage
    0x06,  // interfaceSubclass: SCSI
    0x50,  // interfaceProtocol: BBB
    0,   // string index

    // ================
    7,
    USB_DEVICE_DESCRIPTOR_TYPE_ENDPOINT,
    0x81,    // endpoint number
    0x02,    // bmAttributes (0x02 = bulk data)
    64,      // wMaxPacketSize
    0,
    0,       // bInterval

    // ================
    7,
    USB_DEVICE_DESCRIPTOR_TYPE_ENDPOINT,
    0x02,    // endpoint number
    0x02,    // bmAttributes (0x02 = bulk data)
    64,      // wMaxPacketSize
    0,
    0,       // bInterval
};

int32_t fill_setup_response(volatile usb_device_request_t *req, volatile uint8_t *dest)
{
    uint32_t bytes_filled = 0;
    switch (req->request) {
        // GET_DESCRIPTOR
        case 0x06: {
            usb_device_descriptor_type_e dt = req->value >> 8;
            switch(dt) {
                case USB_DEVICE_DESCRIPTOR_TYPE_DEVICE: {
                    bytes_filled = (req->length > 18) ? 18 : req->length;
                    memcpy((uint8_t*)dest, descriptor, bytes_filled);
                    break;
                }

                case USB_DEVICE_DESCRIPTOR_TYPE_CONFIGURATION: {
                    bytes_filled = (req->length > 32) ? 32 : req->length;
                    memcpy((uint8_t*)dest, configuration_descriptor, bytes_filled);
                    break;
                }

                default: {
                    bytes_filled = -1;
                    break;
                }
            }
            break;
        }

        // SET_CONFIGURATION
        case 0x09: {
            bytes_filled = 0;
            break;
        }

        default: {
            bytes_filled = -1;
            break;
        }
    }

    return bytes_filled;
}




void USB_Handler()
{
    static uint8_t addr = 0;

    static scsi_state_t scsi_state = { 0 };

    // handle usb events
    if (USB->DEVICE.INTFLAG.bit.EORST) {
        SERCOM3_puts("USB reset\r\n");
        USB->DEVICE.DeviceEndpoint[0].EPCFG.bit.EPTYPE0 = 1;
        USB->DEVICE.DeviceEndpoint[0].EPCFG.bit.EPTYPE1 = 1;
        USB->DEVICE.DeviceEndpoint[0].EPINTENSET.bit.RXSTP = 1;
        USB->DEVICE.DeviceEndpoint[0].EPINTENSET.bit.TRCPT0 = 1;
        USB->DEVICE.DeviceEndpoint[0].EPINTENSET.bit.TRCPT1 = 1;

        USB->DEVICE.DeviceEndpoint[1].EPCFG.bit.EPTYPE0 = 0;
        USB->DEVICE.DeviceEndpoint[1].EPCFG.bit.EPTYPE1 = 3;
        USB->DEVICE.DeviceEndpoint[1].EPINTENSET.bit.TRCPT1 = 1;
        // signal that EP1's IN bank presently lacks data.
        USB->DEVICE.DeviceEndpoint[1].EPSTATUSCLR.bit.BK1RDY = 1;
        USB->DEVICE.DeviceEndpoint[1].EPINTENSET.bit.STALL1 = 1;

        USB->DEVICE.DeviceEndpoint[2].EPCFG.bit.EPTYPE0 = 3;
        USB->DEVICE.DeviceEndpoint[2].EPCFG.bit.EPTYPE1 = 0;
        USB->DEVICE.DeviceEndpoint[2].EPINTENSET.bit.TRCPT0 = 1;
        // signal that EP2's OUT bank presently has space for data.
        USB->DEVICE.DeviceEndpoint[2].EPSTATUSCLR.bit.BK0RDY = 1;
        USB->DEVICE.DeviceEndpoint[2].EPINTENSET.bit.STALL0 = 1;

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
                int32_t bytes_to_send = fill_setup_response(&request, (void *)ep0_in_buf);
                if (bytes_to_send < 0) {
                    // STALL
                    SERCOM3_puts("responding with STALL\r\n");
                    USB->DEVICE.DeviceEndpoint[0].EPSTATUSSET.bit.STALLRQ1 = 1;
                } else {
                    endpoint_descriptors[0].DeviceDescBank[1].PCKSIZE.bit.BYTE_COUNT = bytes_to_send;
                    USB->DEVICE.DeviceEndpoint[0].EPSTATUSSET.bit.BK1RDY = 1;
                }
            } else {
                // handle commands without a data stage
                // NB: the only host->device command with a data stage is "SET_DESCRIPTOR"
                if (request.request == 5) {
                    addr = ep0_out_buf[2];
                    endpoint_descriptors[0].DeviceDescBank[1].PCKSIZE.bit.BYTE_COUNT = 0;
                    USB->DEVICE.DeviceEndpoint[0].EPSTATUSSET.bit.BK1RDY = 1;
                }
                else if ((request.request == 1) && (request.index == 0x0081)) {
                    // NB: hacky code. "clear feature" requests should be passed to code that
                    // manages the SCSI stuff, and I should check that it accepts the STALL.
                    endpoint_descriptors[0].DeviceDescBank[1].PCKSIZE.bit.BYTE_COUNT = 0;
                    USB->DEVICE.DeviceEndpoint[0].EPSTATUSSET.bit.BK1RDY = 1;

                    USB->DEVICE.DeviceEndpoint[1].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_STALL1;
                    USB->DEVICE.DeviceEndpoint[1].EPSTATUSCLR.bit.STALLRQ1 = 1;
                    int32_t bytes_to_send = scsi_handle(&scsi_state,
                                                        USB_TRANSFER_DIRECTION_IN,
                                                        ep2_out_buf,
                                                        0,
                                                        ep1_in_buf);
                    if (bytes_to_send >= 0) {
                        SERCOM3_puts("quaz\r\n");
                        endpoint_descriptors[1].DeviceDescBank[1].PCKSIZE.bit.BYTE_COUNT = bytes_to_send;
                        USB->DEVICE.DeviceEndpoint[1].EPSTATUSSET.bit.BK1RDY = 1;
                    }
                } else {
                    endpoint_descriptors[0].DeviceDescBank[1].PCKSIZE.bit.BYTE_COUNT = 0;
                    USB->DEVICE.DeviceEndpoint[0].EPSTATUSSET.bit.BK1RDY = 1;
                }
            }

            USB->DEVICE.DeviceEndpoint[0].EPSTATUSCLR.bit.BK0RDY = 1;

            // clear RXSTP bit
            USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_RXSTP;

            // XXX TODO: there may be a better way to handle this.
            // clear TRCPT0 status
            USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_TRCPT0;
        }

        if (USB->DEVICE.DeviceEndpoint[0].EPINTFLAG.bit.TRCPT0) {
            uint8_t bytes = endpoint_descriptors[0].DeviceDescBank[0].PCKSIZE.bit.BYTE_COUNT;
            SERCOM3_puts("TRCPT, ");
            SERCOM3_putx(bytes);
            SERCOM3_puts(" bytes:\r\n");
            hexprint(ep0_out_buf, bytes);
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

    // handle endpoint 1 stuff
    if (USB->DEVICE.EPINTSMRY.bit.EPINT1) {
        // ugh so much spaghetti
        if (USB->DEVICE.DeviceEndpoint[1].EPINTFLAG.bit.TRCPT1) {
            uint8_t bytes = endpoint_descriptors[1].DeviceDescBank[1].PCKSIZE.bit.BYTE_COUNT;
            SERCOM3_puts("EP1 TX finished:\r\n");
            hexprint((uint8_t*)(ep1_in_buf), bytes);
            SERCOM3_puts("\r\n\r\n");

            int32_t bytes_to_send = scsi_handle(&scsi_state,
                                                USB_TRANSFER_DIRECTION_IN,
                                                ep2_out_buf,
                                                0,
                                                ep1_in_buf);

            // clear the pending interrupt
            USB->DEVICE.DeviceEndpoint[1].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_TRCPT1;

            if (bytes_to_send >= 0) {
                endpoint_descriptors[1].DeviceDescBank[1].PCKSIZE.bit.BYTE_COUNT = bytes_to_send;
                USB->DEVICE.DeviceEndpoint[1].EPSTATUSSET.bit.BK1RDY = 1;
            }
        } else if (USB->DEVICE.DeviceEndpoint[1].EPINTFLAG.bit.STALL1) {
            SERCOM3_puts("EP1 STALL sent.\r\n");
/*            int32_t bytes_to_send = scsi_handle(&scsi_state,
                                                USB_TRANSFER_DIRECTION_IN_STALL,
                                                ep2_out_buf,
                                                0,
                                                ep1_in_buf);*/

            // clear the pending interrupt
            USB->DEVICE.DeviceEndpoint[1].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_STALL1;

/*            if (bytes_to_send >= 0) {
                SERCOM3_puts("baz\r\n");
                endpoint_descriptors[1].DeviceDescBank[1].PCKSIZE.bit.BYTE_COUNT = bytes_to_send;
                USB->DEVICE.DeviceEndpoint[1].EPSTATUSSET.bit.BK1RDY = 1;
                }*/
        }
    }

    if (USB->DEVICE.EPINTSMRY.bit.EPINT2) {
        uint8_t bytes = endpoint_descriptors[2].DeviceDescBank[0].PCKSIZE.bit.BYTE_COUNT;
        int32_t bytes_to_send = scsi_handle(&scsi_state,
                                            USB_TRANSFER_DIRECTION_OUT,
                                            ep2_out_buf,
                                            bytes,
                                            ep1_in_buf);

        SERCOM3_puts("RX CBW: \r\n");
        cbw_print(&(scsi_state.cbw));
        SERCOM3_puts("\r\n");

        USB->DEVICE.DeviceEndpoint[2].EPSTATUSCLR.bit.BK0RDY = 1;
        USB->DEVICE.DeviceEndpoint[2].EPINTFLAG.reg = USB_DEVICE_EPINTFLAG_TRCPT0;

        if (bytes_to_send >= 0) {
            endpoint_descriptors[1].DeviceDescBank[1].PCKSIZE.bit.BYTE_COUNT = bytes_to_send;
            USB->DEVICE.DeviceEndpoint[1].EPSTATUSSET.bit.BK1RDY = 1;
        } else if (bytes_to_send == -2) {
            // TODO: stall IN endpoint
            // make sure to service STALL interrupt
            USB->DEVICE.DeviceEndpoint[1].EPSTATUSSET.bit.STALLRQ1 = 1;
        }
    }

    SERCOM3_puts("\r\n");
}
