/**
 * TODO: this file could use a rename.
 */

#include <stdint.h>

typedef struct usb_device_descriptor
{
    uint16_t usb_version;
    uint8_t  dev_class;
    uint8_t  dev_subclass;
    uint8_t  protocol;
    uint8_t  ep0_max_pct_size;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_release_bcd;
    uint8_t  nconfigs;

    //const configuration_descriptor_t *config_descriptors[2];
    //const char *device_descriptor_strings[3];
} usb_device_descriptor_t;

//void usb_device_descriptor_init(usb_device_descriptor_t*, uint16_t usb_version, );

#pragma pack(push, 1)
typedef struct usb_device_request
{
    uint8_t  request_type;
    uint8_t  request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} usb_device_request_t;
#pragma pack(pop)

typedef enum usb_device_descriptor_type {
    USB_DEVICE_DESCRIPTOR_TYPE_DEVICE                = 1,
    USB_DEVICE_DESCRIPTOR_TYPE_CONFIGURATION         = 2,
    USB_DEVICE_DESCRIPTOR_TYPE_STRING                = 3,
    USB_DEVICE_DESCRIPTOR_TYPE_INTERFACE             = 4,
    USB_DEVICE_DESCRIPTOR_TYPE_ENDPOINT              = 5,
    USB_DEVICE_DESCRIPTOR_TYPE_DEVICE_QUALIFIER      = 6,
    USB_DEVICE_DESCRIPTOR_TYPE_OTH_SPD_CONFIGURATION = 7,
    USB_DEVICE_DESCRIPTOR_TYPE_INTERFACE_POWER       = 8
} usb_device_descriptor_type_e;
