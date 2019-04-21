#include "scsi.h"

void scsi_handle(scsi_state_t *state,
                 usb_transfer_direction_e dir,
                 uint8_t *out_buf,
                 uint8_t *in_buf)
{
    switch (state->current_state) {
        case CBW_FLOW_CBW_STATE: {
            switch () {

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
}
