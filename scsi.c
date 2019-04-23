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

// NB: big endian???
static const uint8_t scsi_capacity[] =
{
    0x00, 0x00, 0x07, 0xff,   // 2048 blocks
    0x00, 0x00, 0x02, 0x00    // 512  bytes per block
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
        case CBW_FLOW_EXPECTING_CBW_STATE: {
            if (dir != USB_TRANSFER_DIRECTION_OUT) {
                state->current_state = CBW_FLOW_ERROR_STATE;
            } else {
                memcpy((void*)&state->cbw, out_buf, out_buf_nbytes);
                state->data_stage_bytes_remaining = state->cbw.cbw_data_transfer_length;
                state->csw.csw_status = 0;
                switch (state->cbw.cbwcb[0]) {
                    case SCSI_COMMAND_INQUIRY: {
                        if (state->cbw.cbw_data_transfer_length > sizeof(scsi_inquiry_response)) {
                            bytes_to_send = sizeof(scsi_inquiry_response);
                        } else {
                            bytes_to_send = state->cbw.cbw_data_transfer_length;
                        }

                        // fill the IN buffer
                        memcpy(in_buf, scsi_inquiry_response, bytes_to_send);

                        // update state
                        state->data_stage_bytes_remaining -= bytes_to_send;
                        state->current_state = CBW_FLOW_DATA_IN_PENDING_STATE;
                        break;
                    }

                    case SCSI_COMMAND_TEST_UNIT_READY: {
                        // construct a csw and send it
                        // NB: lots of opportunity for clearing duplicated code here.
                        memcpy(state->csw.csw_signature, "USBS", 4);
                        state->csw.csw_tag = state->cbw.cbw_tag;
                        state->csw.csw_data_residue = state->data_stage_bytes_remaining;
                        state->csw.csw_status = 0;
                        bytes_to_send = 13;
                        memcpy(in_buf, &(state->csw), bytes_to_send);
                        state->current_state = CBW_FLOW_CSW_PENDING_STATE;
                        break;
                    }

                    case SCSI_COMMAND_READ_CAPACITY_10: {
                        bytes_to_send = 8;
                        memcpy(in_buf, scsi_capacity, bytes_to_send);

                        // update state
                        state->data_stage_bytes_remaining -= bytes_to_send;
                        state->current_state = CBW_FLOW_DATA_IN_PENDING_STATE;
                        break;
                    }

                    default: {
                        // TODO: handle unsupported command: stall BULK-in pipe
                        bytes_to_send = -2;
                        state->csw.csw_status = 1;
                        break;
                    }
                }
            }
            break;
        }

        case CBW_FLOW_EXPECTING_DATA_OUT_STATE: {
            // do the action
            if ((dir != USB_TRANSFER_DIRECTION_OUT) &&
                (dir != USB_TRANSFER_DIRECTION_OUT_STALL)){
                state->current_state = CBW_FLOW_ERROR_STATE;
            } else {
                if (dir == USB_TRANSFER_DIRECTION_OUT) {
                    switch (state->cbw.cbwcb[0]) {
                        default: {
                            // this should never happen
                            state->current_state = CBW_FLOW_DATA_IN_PENDING_STATE;
                            state->csw.csw_status = 1;
                            break;
                        }
                    }
                }

                // TODO: how to handle spaghetti logic if result of switch statement "doesn't want"
                // a CSW sent?
                // Send a CSW based on what happened during the "do it" part.
                memcpy(state->csw.csw_signature, "USBS", 4);
                state->csw.csw_tag = state->cbw.cbw_tag;
                state->csw.csw_data_residue = state->data_stage_bytes_remaining;
                bytes_to_send = 13;
                memcpy(in_buf, &(state->csw), bytes_to_send);

                state->current_state = CBW_FLOW_CSW_PENDING_STATE;
            }
            break;
        }

        case CBW_FLOW_DATA_IN_PENDING_STATE: {
            // just send the CSW
            memcpy(state->csw.csw_signature, "USBS", 4);
            state->csw.csw_tag = state->cbw.cbw_tag;
            state->csw.csw_data_residue = state->data_stage_bytes_remaining;
            bytes_to_send = 13;
            memcpy(in_buf, &(state->csw), bytes_to_send);

            state->current_state = CBW_FLOW_CSW_PENDING_STATE;

            break;
        }

        case CBW_FLOW_CSW_PENDING_STATE: {
            // TODO: if (no error)
            if (1) {
                bytes_to_send = -1;
                state->current_state = CBW_FLOW_EXPECTING_CBW_STATE;
            }
            break;
        }

        case CBW_FLOW_ERROR_STATE: {
            break;
        }
    }

    return bytes_to_send;
}
