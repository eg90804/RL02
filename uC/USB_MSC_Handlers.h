#ifndef USB_MSC_HANDLERS_H_
#define USB_MSC_HANDLERS_H_

#include <stdint.h>

extern void*    usb_RL02_Open(uint32_t driveNum);
extern void     usb_RL02_Close(void *drive);
extern uint32_t usb_RL02_Read(void *drive, uint8_t *data, uint32_t sectorNum, uint32_t count);
extern uint32_t usb_RL02_Write(void *drive, uint8_t *data, uint32_t sectorNum, uint32_t count);
extern uint32_t usb_RL02_BlockCount(void *drive);
extern uint32_t usb_RL02_BlockSize(void *drive);

#endif
