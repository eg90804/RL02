#ifndef USB_MSC_STRUCTURES_H_
#define USB_MSC_STRUCTURES_H_

//Holds all the descriptors and device info passed to the PC
extern tUSBDMSCDevice usb_RL02_DeviceStructure;

//The handler for the usb events is defined in main.c so it can have access to
//whatever resources we want later
extern uint32_t usb_RL02_CallbackHndlr(void *pvDevice, uint32_t event, uint32_t ui32MsgParam, void *pvMsgData);

#endif
