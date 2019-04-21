#include "scsi.h"

#include <string.h>

static const uint8_t scsi_inquiry_response[] =
{
    0,      // direct access block type
    0x80,   // device is "removable"
    4,      // SPC-2 compliance
    2,      // responce data format
    0x20,   // remaining response length
    0,      // additional fields
    0,
    0,
    'j', ' ', 'm', 'a', 'm', 'i', 's', 'h',
    'm', 'a', 's', 's', ' ', 's', 't', 'o', 'r', 'a', 'g', 'e', ' ', ' ', ' ', ' ',
    '0', '0', '0', '1'
};

/**
 * probably will be called from an interrupt context
 */
int32_t scsi_handle(scsi_state_t *state,
                    usb_transfer_direction_e dir,
                    uint8_t *out_buf,       // remember: host ---> mcu
                    uint8_t  out_buf_nbytes,
                    uint8_t *in_buf)        // mcu ---> host
{
    int32_t bytes_to_send = 0;
    switch (state->current_state) {
        case CBW_FLOW_CBW_STATE: {
            if (dir != USB_TRANSFER_DIRECTION_OUT) {
                state->current_state = CBW_FLOW_ERROR_STATE;
            } else {
                memcpy((void*)&state->cbw, out_buf, out_buf_nbytes);
                switch (state->cbw.cbwcb[0]) {
                    case SCSI_COMMAND_INQUIRY: {
                        if (state->cbw.cbw_data_transfer_length > sizeof(scsi_inquiry_response)) {
                            bytes_to_send = sizeof(scsi_inquiry_response);
                        } else {
                            bytes_to_send = state->cbw.cbw_data_transfer_length;
                        }
                        memcpy(in_buf, scsi_inquiry_response, bytes_to_send);
                        state->data_stage_bytes_remaining -= bytes_to_send;
                        break;
                    }

                    default: {
                        // TODO: handle unsupported command: stall BULK-in pipe
                        bytes_to_send = -1;
                    }
                }
            }
            break;
        }

        case CBW_FLOW_DATA_STATE: {
            break;
        }

        case CBW_FLOW_CSW_STATE: {
            break;
        }

        case CBW_FLOW_ERROR_STATE: {
            break;
        }
    }

    return bytes_to_send;
}
