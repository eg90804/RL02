#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "inc/hw_ints.h"
#include "inc/hw_types.h"
#include "inc/hw_memmap.h"

#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/rom.h"
#include "driverlib/ssi.h"
#include "driverlib/sysctl.h"
#include "driverlib/udma.h"
#include "driverlib/usb.h"

#include "usblib/usblib.h"
#include "usblib/usb-ids.h"
#include "usblib/device/usbdevice.h"
#include "usblib/device/usbdmsc.h"

#include "ucpins.h"
#include "USB_MSC_Structures.h"

#define USE_USB_DMA 1

//DMA control data structure
#if defined(ccs)
#pragma DATA_ALIGN(DMA_Ctl, 1024)
tDMAControlTable DMA_Ctl[64];
#else //GCC et al
tDMAControlTable DMA_Ctl[64] __attribute__ ((aligned(1024)));
#endif

unit8_t  RL02_Online = FALSE;
uint8_t  USB_Led = 0;
void    *pvDevice = 0;

// Called when data has been received via USB, must be defined.
uint32_t RxHandler(void *callback, uint32_t event, uint32_t eventMsg, void *eventPtr)
{
  return (0);
}

// Called when an USB write has been executed, must be defined.
uint32_t TxHandler(void *callback, uint32_t event, uint32_t eventMsg, void *eventPtr)
{
  return (0);
}

int main(void)
{
  //
  // 50Mhz from the 200Mhz PLL with a 16Mhz crystal input.
  //
  SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);

#ifdef USE_USB_DMA
  //Turn on the uDMA engine
  SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
  SysCtlDelay(10);
  uDMAControlBaseSet(&DMA_Ctl[0]);
  uDMAEnable();
#endif

  // Initialize the GPIO pins
  PortFunctionInit();

  GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1); //Put FPGA core into reset (this is the case when not driving the pin).
  GPIOPinWrite(GPIO_PORTA_BASE, GPIO_INT_PIN_6, 0); //De-Assert Write Enable
  SysCtlDelay(SysCtlClockGet() / 30); //Wait ~100ms

  GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, 0); //Bring the FPGA out of reset once we start up
  SysCtlDelay(SysCtlClockGet() / 30); //Wait ~100ms

  // Force the uC into USB device mode (the sense pin may not work due to a silicon bug)
  USBStackModeSet(0, eUSBModeForceDevice, 0);

  // Setup the SPI interface, Normal mode, 16-bit transfer width
  SSIClockSourceSet(SSI0_BASE, SSI_CLOCK_PIOSC);
  // We can't drive the SPI from the 50Mhz system clock (for stupid technical reasons)
  // so drive it from the 16Mhz onboard osc
  SSIConfigSetExpClk(SSI0_BASE, 16000000, SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, 8000000, 16);
  SSIEnable(SSI0_BASE);

  //Startup the USB Device
  pvDevice = USBDMSCInit(0, &usb_RL02_DeviceStructure);
  USBDMSCMediaChange(pvDevice, eUSBDMSCMediaUnknown);
/*
  Future : Have 2nd (serial) device to act as a status register.
           Will require modifications in simh RL driver as well
           The name could reflect the drive number (white drive
           number plug at the front of the drive), f.e /dev/rl02sts00
*/

  //TODO we should allow the attachment of the drive over USB before it is spun up once we have drive status information
  uint32_t zero;
  while (SSIDataGetNonBlocking(SSI0_BASE, &zero)); // Clear the SPI FIFO buffer of possible junk
  while (GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_7));  //Wait for valid data from the Drive
  while (!GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_2));  //Wait for the command FIFO (on the drive) to empty
  USBDMSCMediaChange(pvDevice, eUSBDMSCMediaPresent);
  RL02_Drive_Online = TRUE;

  while (1) {
/*
    if (USB_Led & RL02_WRITE)
      GPIOPinWrite(GPIO_PORTX_BASE, GPIO_PIN_X, GPIO_PIN_X);
    else
      GPIOPinWrite(GPIO_PORTX_BASE, GPIO_PIN_X, 0);

    if (USB_Led_read & RL02_READ)
      GPIOPinWrite(GPIO_PORTY_BASE, GPIO_PIN_Y, GPIO_PIN_Y);
    else
      GPIOPinWrite(GPIO_PORTY_BASE, GPIO_PIN_Y, 0);
*/
    }

  if (pvDevice != 0)
    USBMSCTerm(pvDevice);
}

