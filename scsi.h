#ifndef SCSI_H
#define SCSI_H

#pragma pack(push, 1)
typedef struct usb_mass_storage_cbw
{
    uint32_t cbw_signature;
    uint32_t cbw_tag;
    uint32_t cbw_data_transfer_length;
    uint8_t  cbw_flags;
    uint8_t  cbw_lun;
    uint8_t cbwcb_length;
    uint8_t cbwcb[16];
} usb_mass_storage_cbw_t;
#pragma pack(pop)

typedef enum cbw_flow {
    CBW_FLOW_CBW_STATE,
    CBW_FLOW_DATA_STATE,
    CBW_FLOW_CSW_STATE,
    CBW_FLOW_ERROR_STATE
} cbw_flow_e;

typedef enum usb_transfer_direction {
    USB_TRANSFER_DIRECTION_OUT,
    USB_TRANSFER_DIRECTION_IN
} usb_transfer_direction_e;

typedef struct scsi_state {
    usb_mass_storage_cbw_t cbw;
    cbw_flow_e current_state;
    uint32_t data_stage_bytes_remaining;

} scsi_state_t;


#define SCSI_COMMAND_INQUIRY 0x12

void scsi_handle(scsi_state_t *state,
                 usb_transfer_direction_e dir,
                 uint8_t *out_buf,
                 uint8_t *in_buf);


#endif
