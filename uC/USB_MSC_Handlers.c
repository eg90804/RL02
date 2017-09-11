#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"

#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "driverlib/rom.h"
#include "driverlib/ssi.h"
#include "driverlib/sw_crc.h"
#include "driverlib/sysctl.h"
#include "driverlib/systick.h"

#include "usblib/usblib.h"
#include "usblib/usb-ids.h"
#include "usblib/device/usbdevice.h"
#include "usblib/device/usbdmsc.h"

#include "USB_MSC_Structures.h"

// TODO File is a mess, re-architect so that the reading logic is smarter
// regarding seeks. Ideally, the controller will cache things as they fly
// by, but without a threading library that isn't going to support
// interruption by USB very well.  Alternatively, the seek logic should be
// folded into the read USB function

uint16_t *SectorBuffer;
uint_fast8_t SectorBufferValid[40];
uint_fast16_t PrevSeekTrack;
uint_fast8_t PrevSeekHead;

uint32_t i;

extern uint8_t RL02_Online;
extern uint8_t USB_Led;

#define RL_DEBUG 0	// For debugging puposes only

#define DRIVE_ATTACHED 1
#define DRIVE_LOADED   2

#define ERR_INVALID_PARAMS  -1
#define ERR_HEADER_CRC_FAIL -2
#define ERR_SEEK_FAILED     -3

struct
{
  uint_fast8_t driveFlags;
} driveInstance;

void SPITx(uint32_t tx)
{
  uint32_t dummy = 0;
  SSIDataPut(SSI0_BASE,tx);
  while(SSIBusy(SSI0_BASE)) ;
  SSIDataGet(SSI0_BASE, &dummy);
}

void SPIRx(uint32_t *rx)
{
  uint32_t dummy;
  SSIDataPut(SSI0_BASE,0);
  while (SSIBusy(SSI0_BASE)) ;
  if (!rx) {
    SSIDataGet(SSI0_BASE, &dummy);
    }
    else {
      SSIDataGet(SSI0_BASE, rx);
    }
}

void waitForData()
{
  while(GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_7)) ;  //Wait for valid data
}

//This is the first pass at the seek function.
//Now that crunch time is over, the entire USB end needs some rearchitecting
//
int32_t seek(void *drive, uint_fast8_t head, uint_fast16_t track)
{
  if (drive == 0)
    return ERR_INVALID_PARAMS;

  uint32_t FailedSeek = 0;
  uint32_t BadSectorHeader = 0;

  uint_fast16_t LastCyl = 0xFFFF;
  uint_fast8_t LastHead = 0xFF;

  uint_fast8_t JustReturnCurSector = 0;

  uint32_t position = 0;

  uint32_t HeaderWord = 0;
  uint32_t ReservedWord = 0;
  uint32_t CRC = 0;

  if (head<0 || head>1 || track<0 || track>511) {
#ifdef RL_DEBUG
    while (1);  //Trap here for examination
#endif
    return ERR_INVALID_PARAMS;
    }

  //TODO We really shouldn't just trust that the heads are where we left them
  if (PrevSeekTrack == track && PrevSeekHead == head) {
    JustReturnCurSector = 1;
    }

  while(1) { //Breakout on successful location of track or unrecoverable failure
    // TODO There is a timing problem here because the FPGA may not have yet
    // asserted that data is invalid (if the FPGA gets a command,
    // it should assert data WAIT directly)
    while (GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_7)) ;  //Wait for valid data
    if (!GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_6)) { //If this isn't a header word
      SPIRx(0);
      }
    else { //If this is a header word
      uint16_t CalculatedCRC;
      uint8_t CrcPack[2];

      //Store the word in the proper place
      SPIRx(&HeaderWord);

      CrcPack[0] = HeaderWord & 0xFF;
      CrcPack[1] = (HeaderWord>>8) & 0xFF;
      CalculatedCRC = Crc16(0, (const uint8_t*)CrcPack, 2);

      waitForData();
      SPIRx(&ReservedWord);

      CrcPack[0] = ReservedWord & 0xFF;
      CrcPack[1] = (ReservedWord>>8) & 0xFF;
      CalculatedCRC = Crc16(CalculatedCRC, (const uint8_t*)CrcPack, 2);

      waitForData();
      SPIRx(&CRC);

      if (CRC != CalculatedCRC) {
        BadSectorHeader++;
        if (BadSectorHeader>40) { // If there is a truly bad header, there will
                                  // be a 50ms penalty before giving up, but we
                                  // are insensative to a run of 2 bad sectors
#ifdef RL_DEBUG
          while(1) ;  //Trap here for examination
#endif
          return ERR_HEADER_CRC_FAIL;
          }
        continue; //Look for another sector
        }

      //If we've previously calculated that we don't need to seek
      if (JustReturnCurSector) {
        return HeaderWord & 0b111111;
        }

      //Do some bit-fu on the incoming data
      uint_fast16_t cylFromDrive = (HeaderWord>>7) & 0b111111111;
      uint_fast8_t headFromDrive = (HeaderWord>>6) & 0b1;

      LastCyl = cylFromDrive;
      LastHead = headFromDrive;

      //If we've landed on the requested track
      if (cylFromDrive == track && headFromDrive == head) {
        PrevSeekHead  = head;
        PrevSeekTrack = track;
        return HeaderWord & 0b111111; //We are done seeking, return the current sector
        }
      else { //If we need to move still, issue our seek command
             //This is the command word to the FPGA
        position = 0b0010000000000000;

        if (track>cylFromDrive) { //If we need to move inwards
          position |= (track-cylFromDrive);
          position |= 1<<9; //Move towards the spindle
          }
        else { //If we need to move outwards
          position |= (cylFromDrive-track);
          }
        position |= head<<10;

        //The drive is mis-seeking and we're stuck on the same track for more than 3 iterations
        //Perterb the heads additional tracks. At least two drives were observed with this problem
        if (FailedSeek%4==3 && cylFromDrive==LastCyl && headFromDrive==LastHead) {
          if (cylFromDrive < 255) { //if we're stuck on the outward part of the disk
            position += 1; //Lets try to move an extra track inward (2 tracks then 3, etc.)
            }
          else { //if we're stuck on the inward part of the disk
            position -= 1; //Lets try to move an additional track outward
            }
          }

        if (FailedSeek > 15) {
          //TODO Maybe record the offending seeks
#ifdef RL_DEBUG
          while(1) ;  // Trap here for examination
#endif
          return ERR_SEEK_FAILED;
          }

        while(!GPIOPinRead(GPIO_PORTF_BASE, GPIO_PIN_2)) ;  //Wait for the command FIFO to empty
        //Issue the move command
        SPITx(position);

        //Invalidate the read cache
        for (i = 0; i<40; i++) {
          SectorBufferValid[i] = 0;
          }

        //Track the number of seek commands sent to the drive this function call
        FailedSeek++;
        }
      }
    }
}

// The Open function
void* usb_RL02_Open(uint32_t driveNum)
{
  uint_fast8_t i;

  if (driveNum > 0 || RL02_Online == FALSE || 
      driveInstance.driveFlags != 0) {
    return 0;

  if (!SectorBuffer) {
    SectorBuffer = (uint16_t*)calloc(40*128,sizeof(uint16_t));
    for (i = 0; i<40; i++) {
      SectorBufferValid[i] = 0;
      }
    if (!SectorBuffer) {
      return 0;
      }
    }

  //TODO Check the drive state field and report just DRIVE_ATTACHED if we are anything but loaded

  //We can now report that the drive is attached and busy (because we are doing things with it)
  driveInstance.driveFlags = DRIVE_ATTACHED | DRIVE_LOADED;
  USBDMSCMediaChange(pvDevice, eUSBDMSCMediaPresent);

  return ((void *) &driveInstance);
}


// The Close function
void usb_RL02_Close(void *drive)
{
  if (drive == 0 || RL02_Online == FALSE)
    return;

  free(SectorBuffer);
  // Reset drive?
  driveInstance.driveFlags = 0;
}


// The Read function
uint32_t usb_RL02_Read(void *drive, uint8_t *data, uint32_t sectorNum, uint32_t count)
{
  if (drive == 0 || RL02_Online == FALSE)
    return 0;

  sectorNum *= 2;
  count *= 2;

  uint_fast8_t sector = 0;
  uint_fast8_t head = 0;
  uint_fast16_t track = 0;

  uint_fast32_t CompletedBlocks = 0;
  while (CompletedBlocks<count) {
    //Looks like simh uses this layout for logical access. It's not physically optimal.
    head = sectorNum+CompletedBlocks > 20479 ? 1 : 0;
    sector = (sectorNum+CompletedBlocks) % 40;
    track = ((sectorNum+CompletedBlocks) / 40) % 512;

    //This is how the RL02 User guide lists linear access
    /*/ /Entire track, alternating heads
    head = (sectorNum+CompletedBlocks) % 80 > 39 ? 1 : 0;
    sector = ((sectorNum+CompletedBlocks) % 40);
    track = ((sectorNum+CompletedBlocks) / 80) % 512;
    */

    int32_t ret;
    uint32_t dontCare;
    uint32_t HeaderWord;
    uint32_t ReservedWord;
    uint32_t HeaderCRCWord;
    uint32_t DataWord;
    uint32_t dataCRCWord;

    uint_fast8_t SectorNumberFromDrive;

    ret = seek(drive, head, track); //Seeks are internally a NOP if we're on it.
    if (ret<0) { //If the seek failed
      //There's no way to indicate what type of failure occured to the PC, so just tell it we're returning 0 sectors
      return 0;
      }
    SectorNumberFromDrive = ret;

    if (SectorBufferValid[sector]) { //If the data we are looking for is in cache
      for (i=0; i<128; i++) { //Read it out from cache
        data[(i*2) + (CompletedBlocks*2*132)] = SectorBuffer[(sector*128)+i] & 0xFF;
        data[(i*2) + (CompletedBlocks*2*132) + 1] = SectorBuffer[(sector*128)+i]>>8 & 0xFF;
        }
      CompletedBlocks++;
      }
    else { //If our data is not in cache
      while (SectorNumberFromDrive != sector) {
        if (!SectorBufferValid[SectorNumberFromDrive]) { //If the cache needs to be updated
          for (i = 0; i<128; i++) {
            waitForData();
            SPIRx(&DataWord);

            SectorBuffer[i+(SectorNumberFromDrive*128)] = DataWord; //Cache the value

            if (i == 127) { //Last Word
              //TODO Check the data CRC
              waitForData();
              SPIRx(&dataCRCWord);
              }
            }
          SectorBufferValid[SectorNumberFromDrive] = 1; //Mark the sector as cached
          }
        else { //Else we need to pop off words until we find another sector
          while(1) {
            waitForData();
            if (!GPIOPinRead(GPIO_PORTA_BASE, GPIO_PIN_6)) { //If this isn't a header word
              //Discard a word
              SPIRx(&dontCare);
              }
            else {
              waitForData();
              SPIRx(&HeaderWord);
              waitForData();
              SPIRx(&ReservedWord);
              waitForData();
              SPIRx(&HeaderCRCWord);

              //TODO Check CRC

              SectorNumberFromDrive = HeaderWord & 0b111111;

              break; //If this is a header word, get out of this loop and check it
              }
            }
          }
        }

      //At this point, we're on the right track, right head, right sector

      uint16_t CalculatedCRC = 0;
      for (i = 0; i<128; i++) {
         uint8_t CrcPack[2];

         waitForData();
         SPIRx(&DataWord);

         // TODO Figure out the word/byte swapping necessary for the common data
         // storage mode (8/12/16 bit disk storage mode? Need expert information)
         data[(i*2) + (CompletedBlocks*2*132)] = DataWord & 0xFF;
         data[(i*2) + (CompletedBlocks*2*132) + 1] = DataWord>>8 & 0xFF;
         SectorBuffer[i+(SectorNumberFromDrive*128)] = DataWord; //Cache the value

         CrcPack[0] = DataWord & 0xFF;
         CrcPack[1] = (DataWord>>8) & 0xFF;
         CalculatedCRC = Crc16(CalculatedCRC, (const uint8_t*)CrcPack, 2);

         if (i == 127) { //Last Word
           //TODO Check the data CRC
           waitForData();
           SPIRx(&dataCRCWord);
           }
         }
       SectorBufferValid[SectorNumberFromDrive] = 1; //Mark the sector as cached
       CompletedBlocks++;
       }
    }
    return(count * 256);
}


// The Read function
uint32_t usb_RL02_Write(void *drive, uint8_t *data, uint32_t sectorNum, uint32_t count)
{
  if (drive == 0 || RL02_Online == FALSE)
    return 0;

  sectorNum *= 2;
  count *= 2;

  uint_fast8_t sector = 0;
  uint_fast8_t head = 0;
  uint_fast16_t track = 0;

  uint_fast32_t CompletedBlocks = 0;
  while (CompletedBlocks<count) {
    //Looks like simh uses this layout for logical access. It's not physically optimal.
    head = sectorNum+CompletedBlocks > 20479 ? 1 : 0;
    sector = (sectorNum+CompletedBlocks) % 40;
    track = ((sectorNum+CompletedBlocks) / 40) % 512;

    //This is how the RL02 User guide lists linear access
    /*/ /Entire track, alternating heads
    head = (sectorNum+CompletedBlocks) % 80 > 39 ? 1 : 0;
    sector = ((sectorNum+CompletedBlocks) % 40);
    track = ((sectorNum+CompletedBlocks) / 80) % 512;
    */

    int32_t ret;
    uint32_t DataWord;

    ret = seek(drive, head, track); //Seeks are internally a NOP if we're on it.
    if (ret<0) { //If the seek failed
      //There's no way to indicate what type of failure occured to the PC, so just tell it we're returning 0 sectors
      return 0;
      }

    DataWord = 0b0100000000000000; //The Write Sector Word
    SPITx(DataWord); //Command the drive to write
    DataWord = (sector & 0b111111);//Give the drive the sector we want to write
    SPITx(DataWord);

    //Data Preamble
    DataWord = 0;
    SPITx(DataWord);
    SPITx(DataWord);
    SPITx(DataWord);

    SectorBufferValid[sector] = 0; //Mark the sector as not cached

    uint16_t CalculatedCRC = 0;
    for (i = 0; i<128; i++) {
      uint8_t CrcPack[2];

      DataWord = data[(i*2)+(CompletedBlocks*256)] & 0xFF;
      DataWord |= (data[(i*2)+(CompletedBlocks*256)+1] & 0xFF) << 8;

      CrcPack[0] = DataWord;
      CrcPack[1] = (DataWord>>8);
      CalculatedCRC = Crc16(CalculatedCRC, (const uint8_t*)CrcPack, 2);

      SPITx(DataWord);

      SectorBuffer[i+(sector*128)] = DataWord; //Cache the value

      if (i == 127) { //Last Word
        SPITx(CalculatedCRC);
        }
      }

    DataWord = 0;//Data Postamble
    SPITx(DataWord);

    CompletedBlocks++;
    //At this point, the FPGA should begin writting data to the drive
    }

  return(count * 256);
}


// The Ioctl (Count) function
uint32_t usb_RL02_BlockCount(void *drive)
{
  // Maybe exclude the last Cyl as it contains the bad sector information.
  // In the future, we can read it out and map around the bad sectors.
  // TODO This will need to change for the RL01. We can probably auto-detect
  // by doing a max seek and reading the resulting track
  // (512 for RL02, 256 for RL01)
  if (RL02_Online == FALSE)
    return 0;
  else
    return 20480;
}


// The Blocksize function
uint32_t usb_RL02_BlockSize(void *drive)
{
  // Report false sectorsize as Linux requires it on a 512 byte boundary.
  // The software will handle it.
  return 512;
}


//This function gets called on the completion of USB related service events.
uint32_t usb_RL02_CallbackHndlr(void *callback, uint32_t event, uint32_t eventMsg, void *eventPtr)
{
  switch(event) {
    case USBD_MSC_EVENT_WRITING: {
      USB_Led &= 0x01;
      break;
      }
    case USBD_MSC_EVENT_READING: {
      USB_Led &= 0x02;
      break;
      }
    case USBD_MSC_EVENT_IDLE: {
      USB_Led = 0;
      break;
      }
    default: {
      // All other cases
      break;
      }
    }
  return (0);
}

