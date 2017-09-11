#include <stdbool.h>
#include <stdint.h>

#include "driverlib/usb.h"

#include "usblib/usblib.h"
#include "usblib/usb-ids.h"
#include "usblib/device/usbdevice.h"
#include "usblib/device/usbdmsc.h"

#include "USB_MSC_Handlers.h"
#include "USB_MSC_Structures.h"

//Needed by usblib
#define MSC_BUFFER_SIZE 512
#define DESCRIPTOR_TABLE_SIZE ( sizeof(usbDescriptorTable)/sizeof(uint8_t *) )

//Supported Languages table
const uint8_t usbDescriptor_language[] = {
	4, USB_DTYPE_STRING,
	USBShort(USB_LANG_EN_US)
	};

//Manufacturer
const uint8_t usbDescriptor_manufacturer[] = {
	(10 + 1) * 2, USB_DTYPE_STRING,
	'A', 0, 'C', 0, 'M', 0, 'E', 0, ' ', 0, 'c', 0, 'o', 0, 'r', 0, 'p', 0
	'.', 0
	};

//Product name reported to the PC
const uint8_t usbDescriptor_product[] = {
	(19 + 1) * 2, USB_DTYPE_STRING,
	'R', 0, 'L', 0, '-', 0, '0', 0, '2', 0, ' ', 0, 'U', 0, 'S', 0, 'B', 0,
	' ', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0,
	'e', 0
	};

//Serial number
const uint8_t usbDescriptor_serialNumber[] = {
	(11 + 1) * 2, USB_DTYPE_STRING,
	'P', 0, 'R', 0, 'O', 0, 'T', 0, 'O', 0, 'T', 0, 'Y', 0, 'P', 0, 'E', 0,
	' ', 0, '3', 0
	};

//USB interface descriptor (USB MSC uses bulk transport)
const uint8_t usbDescriptor_interface[] = {
	(19 + 1) * 2, USB_DTYPE_STRING,
	'B', 0, 'u', 0, 'l', 0, 'k', 0, ' ', 0, 'D', 0, 'a', 0, 't', 0, 'a', 0,
	' ', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0,
	'e', 0
	};

//USB configuration type 
const uint8_t usbDescriptor_config[] = {
	(23 + 1) * 2, USB_DTYPE_STRING,
	'B', 0, 'u', 0, 'l', 0, 'k', 0, ' ', 0, 'D', 0, 'a', 0, 't', 0, 'a', 0,
	' ', 0, 'C', 0, 'o', 0, 'n', 0, 'f', 0, 'i', 0, 'g', 0, 'u', 0, 'r', 0,
	'a', 0, 't', 0, 'i', 0, 'o', 0, 'n', 0
	};

//Table holds all these USB descriptor strings so they can be pointed to
const uint8_t * const usb_RL02_DescriptorTable[] = {
	usbDescriptor_language,
	usbDescriptor_manufacturer,
	usbDescriptor_product,
	usbDescriptor_serialNumber,
	usbDescriptor_interface,
	usbDescriptor_config
	};

const tMSCDMedia usb_RL02_MediaFunctions = {
	usb_RL02_Open,
	usb_RL02_Close,
	usb_RL02_Read,
	usb_RL02_Write,
	usb_RL02_BlockCount,
	usb_RL02_BlockSize,
	};


//The device structure is what is passed (via the USB library) to the PC
tUSBDMSCDevice usb_RL02_DeviceStructure = {
	USB_VID_TI_1CBE,	  //Vendor ID, we use the one provided by TI
	USB_PID_MSC,		  //Product ID, we use the one provided by TI
	"OSHW    ",		  //Vendor, 8 fixed characters
	"RL02 USB ADAPTER",	  //Product Name, 16 fixed characters
	"0.02",			  //Version Number
	250,			  //250Ma
	USB_CONF_ATTR_BUS_PWR,	  //This is a bus powered device
	usb_RL02_DescriptorTable, //Here's the pointer to the usb descriptors
	DESCRIPTOR_TABLE_SIZE,	  //Byte size of descriptor table
	usb_RL02_MediaFunctions,  //Targets for the USB callback handler
	usb_RL02_CallbackHndlr,	  //The actual handler for the USB callbacks
	tMSCInstance sPrivateData
	};

