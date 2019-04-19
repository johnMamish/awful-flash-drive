/**
 * TODO: this file could use a rename.
 */

#include <stdint.h>

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
