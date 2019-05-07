#ifndef SCSI_H
#define SCSI_H

#include <stdint.h>

#pragma pack(push, 1)
typedef struct usb_mass_storage_cbw
{
    uint32_t cbw_signature;
    uint32_t cbw_tag;
    // TODO: this should either be unsigned or I should confirm that it will never be > 0x7fffffff.
    int32_t cbw_data_transfer_length;
    uint8_t  cbw_flags;
    uint8_t  cbw_lun;
    uint8_t cbwcb_length;
    uint8_t cbwcb[16];
} usb_mass_storage_cbw_t;
#pragma pack(pop)


#pragma pack(push, 1)
typedef struct usb_mass_storage_csw
{
    uint8_t  csw_signature[4];
    uint32_t csw_tag;
    int32_t  csw_data_residue;
    uint8_t  csw_status;
} usb_mass_storage_csw_t;
#pragma pack(pop)

typedef enum cbw_flow {
    CBW_FLOW_EXPECTING_CBW_STATE,
    CBW_FLOW_DATA_IN_PENDING_STATE,
    CBW_FLOW_EXPECTING_DATA_OUT_STATE,
    CBW_FLOW_CSW_PENDING_STATE,
    CBW_FLOW_ERROR_STATE
} cbw_flow_e;

typedef enum usb_transfer_direction {
    USB_TRANSFER_DIRECTION_OUT,
    USB_TRANSFER_DIRECTION_IN,
    USB_TRANSFER_DIRECTION_OUT_STALL,
    USB_TRANSFER_DIRECTION_IN_STALL
} usb_transfer_direction_e;

typedef struct scsi_state {
    usb_mass_storage_cbw_t cbw;
    usb_mass_storage_csw_t csw;
    cbw_flow_e current_state;

    // TODO: this should either be unsigned or I should confirm that it will never be > 0x7fffffff.
    int32_t data_stage_bytes_remaining;
} scsi_state_t;


#define SCSI_COMMAND_TEST_UNIT_READY 0x00
#define SCSI_COMMAND_INQUIRY 0x12

// according to Jan Axelson's book, this command is not a mandatory SCSI command, but if I STALL it,
// my laptop seems to throw a fit by trying to disable my BBB IN endpoint. Indeed, according to
// wireshark I'm properly STALLing the command, and immediately my laptop sends a "clear feature"
#define SCSI_COMMAND_MODE_SENSE_6 0x1a

#define SCSI_COMMAND_READ_CAPACITY_10 0x25
/**
 * Returns number of bytes processed, 0 indicates that a ZLP should be sent.
 * Returns -1 if there is no data to send
 * Returns -2 if a STALL should be placed in the IN direction
 */
int32_t scsi_handle(scsi_state_t *state,
                    usb_transfer_direction_e dir,
                    uint8_t *out_buf,
                    uint8_t out_buf_nbytes,
                    uint8_t *in_buf);

void scsi_clear_feature_in(scsi_state_t *state);

#endif
