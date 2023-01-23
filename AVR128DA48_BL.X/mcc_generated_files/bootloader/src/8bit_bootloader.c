/**
 *
 * @file 8bit_bootloader.c
 *
 * @ingroup generic_bootloader_8bit
 *
 * @brief This source file provides the implementation of the APIs for the 8-bit Bootloader library
 *
 * @version BOOTLOADER Driver Version 3.0.0
 */

/*
© [2023] Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip 
    software and any derivatives exclusively with Microchip products. 
    You are responsible for complying with 3rd party license terms  
    applicable to your use of 3rd party software (including open source  
    software) that may accompany Microchip software. SOFTWARE IS ?AS IS.? 
    NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS 
    SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT,  
    MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT 
    WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY 
    KIND WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF 
    MICROCHIP HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE 
    FORESEEABLE. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP?S 
    TOTAL LIABILITY ON ALL CLAIMS RELATED TO THE SOFTWARE WILL NOT 
    EXCEED AMOUNT OF FEES, IF ANY, YOU PAID DIRECTLY TO MICROCHIP FOR 
    THIS SOFTWARE.
*/

//   Memory Map
//   ----------------
//   |   0x000000   |   PROGMEM_START
//   |              |
//   |      |       |
//   |     BOOT     |  (this program)
//   |      |       |
//   |              |
//   |   Boot End   |   FUSE_BOOTSIZE * PROGMEM_PAGE_SIZE
//   |              |
//   |      |       |
//   |     APP      |   (User program space)
//   |      |       |
//   |              |
//   |   App End    |   FUSE_CODESIZE * PROGMEM_PAGE_SIZE
//   |              |
//   |      |       |
//   |   APPDATA    |   App Data Space
//   |      |       |
//   |              |
//   |   0x20000   |   PROGMEM_END
//   ----------------
//
// *****************************************************************************

#include <stdbool.h>
#include "../bl_bootload.h"
#include "../bl_communication_interface.h"
#include <stdlib.h>
#include "../../timer/delay.h"
#include <string.h>

//****************************************
// Default Functions (Always Used)
static uint8_t BL_GetVersionData(void);
static void BL_RunBootloader(void);
static bool BL_BootloadRequired(void);
static void BL_CheckDeviceReset(void);
static uint8_t BL_WriteFlash(void);
static uint8_t BL_EraseFlash(void);
static uint16_t BL_ProcessBootBuffer(void);

//****************************************
// Conditional Functions
static uint16_t BL_ReadFlash(void);
static uint16_t BL_ReadConfig(void);
static uint16_t BL_ReadEEData(void);
static uint8_t BL_WriteEEData(void);


typedef void(*app_t)(void);
static uint8_t BL_Reset(void);


// *****************************************************************************
static bool resetPending = false;

// The data frame used for
// holding the current data frame throughout
// boot operation
static frame_t frame;

/*
 * @todo Documentation Needed
 */
void BL_Initialize(void)
{
    resetPending = false;

    BL_INDICATOR_OFF();

    if (BL_BootloadRequired() == true)
    {
        DELAY_milliseconds(200U);

        BL_INDICATOR_ON();
        BL_RunBootloader(); // generic comms layer
    }
    BL_INDICATOR_OFF();
    app_t app = (app_t)(START_OF_APP / sizeof(app_t));
    app();    
}

static bool BL_BootloadRequired(void)
{
    bool status;
    /* NOTE: This function can be customized by the user to enter the bootloader 
     * as required. The entry point can be reading a push button status, a  
     * command from a peripheral, or any other method selected by the user.  
     * 
     * Currently the bootloader checks an IO pin at programming time to force entry into bootloader.
    */  

    // #info  "You may need to add additional delay here between enabling weak pullups and testing the pin."
    for (uint8_t i = 0U; i != 0xFFU; i++)
    {
        asm("nop");
    }
    if (IO_PIN_ENTRY_GetInputValue() == IO_PIN_ENTRY_RUN_BL)
    {
            return (true);                
    }
    if (BL_bootVerify() == false)
    {
        status = true;
    }
    else
    {
        status = false;
    }

    return status;
}

/**
 * @ingroup generic_bootloader_8bit
 * @brief Processes the command header and returns the length of the return packet.
 * @param none
 * @retval The total length of the packet being passed back to the host.
 */
static uint16_t BL_ProcessBootBuffer(void)
{
    uint16_t len;
    switch (frame.command)
    {
    case READ_VERSION:
        len = BL_GetVersionData();
        break;
    case READ_FLASH:
        len = BL_ReadFlash();
        break;
    case WRITE_FLASH:
        len = BL_WriteFlash();
        break;
    case ERASE_FLASH:
        len = BL_EraseFlash();
        break;
    case READ_EE_DATA:
        len = BL_ReadEEData();
        break;
    case WRITE_EE_DATA:
        len = BL_WriteEEData();
        break;
    case READ_CONFIG:
        len = BL_ReadConfig();
        break;
    case RESET_DEVICE:
        frame.data[0] = COMMAND_SUCCESS;
        len = BL_Reset();
        break;
    default:
        frame.data[0] = ERROR_INVALID_COMMAND;
        len = 10U;
        break;
    }
    return (len);
}

static void BL_RunBootloader(void)
{
    uint16_t messageLength = 0U;
    uint16_t index = 0U;
    uint8_t ch;

    while (1)
    {
        BL_CheckDeviceReset();

        BL_CommunicationModuleInit();

        index = 0U; //Point to the buffer
        messageLength = BL_HEADER; // message has 9 bytes of overhead (Synch + Opcode + Length + Address)
        memset(frame.buffer,0,sizeof(frame));    // Clear Buffer
        while (index < messageLength)
        {
            BL_CommunicationModuleRead(&ch, 1);
            frame.buffer[index] = ch;

            index++;
            if (index == 5U)
            {
                if ((frame.command == WRITE_FLASH)
                        || (frame.command == WRITE_EE_DATA)
                        || (frame.command == WRITE_CONFIG))
                {
                    messageLength += frame.data_length;
                }
                else
                {
                    //do nothing
                }
            }
        }

        messageLength = BL_ProcessBootBuffer();

        if (messageLength > 0U)
        {
            BL_CommunicationModuleWrite(frame.buffer, messageLength);

            while (BL_CommunicationModuleIsReady() != true)
            {

            }
        }
    }
}

static void BL_CheckDeviceReset(void)
{
    if (resetPending == true)
    {
        DELAY_milliseconds(3U);
            ccp_write_io((void *)&RSTCTRL.SWRR, RSTCTRL_SWRST_bm);
    }
}

// ******************************************************************************
// Get Bootloader Version Information
//        Cmd     Length----------------   Address---------------
// In:   [|0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00|]
// OUT:  [|0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | VERL | VERH|]
// ******************************************************************************

static uint8_t BL_GetVersionData(void)
{
    uint8_t dataIndex = 0U;
    uint32_t maxPacketSize = 0U;

    maxPacketSize = (PROGMEM_SIZE / ((uint32_t) PROGMEM_PAGE_SIZE));
    device_id_data_t deviceId = SIGROW_DeviceIDRead(SIGNATURES_START);

    // Bootloader Firmware Version
    frame.data[dataIndex] = MINOR_VERSION;
    dataIndex++;
    frame.data[dataIndex] = MAJOR_VERSION;
    dataIndex++;
    frame.data[dataIndex] = BL_HEADER;
    dataIndex++;

    frame.data[dataIndex] = SIGROW_Read(FUSES_START + 0x08U); // BOOT FUSE
    dataIndex++;
    frame.data[dataIndex] = SIGROW_Read(FUSES_START + 0x07U); // APP FUSE
    dataIndex++;

    // max packet size in hexadecimal
    frame.data[dataIndex] = (uint8_t) (maxPacketSize & 0xFFU);
    dataIndex++;
    frame.data[dataIndex] = (uint8_t) ((maxPacketSize >> 8U) & 0xFFU);
    dataIndex++;

    // Unused Bytes
    frame.data[dataIndex] = 0U;
    dataIndex++;
    frame.data[dataIndex] = 0U;
    dataIndex++;

    // device id
    frame.data[dataIndex] = (uint8_t) deviceId;
    dataIndex++;
    frame.data[dataIndex] = (uint8_t) (deviceId >> 8U);
    dataIndex++;
    frame.data[dataIndex] = (uint8_t)(deviceId >> 16U);
    dataIndex++;
    frame.data[dataIndex] = (uint8_t) (PROGMEM_PAGE_SIZE & 0xFFU);
    dataIndex++;
    frame.data[dataIndex] = (uint8_t) (((uint16_t)PROGMEM_PAGE_SIZE >> 8U) & 0xFFU);
    dataIndex++;

    frame.data[dataIndex] = SIGROW.SERNUM0;
    dataIndex++;
    frame.data[dataIndex] = SIGROW.SERNUM1;
    dataIndex++;
    frame.data[dataIndex] = SIGROW.SERNUM2;
    dataIndex++;
    frame.data[dataIndex] = SIGROW.SERNUM3;
    dataIndex++;
    frame.data[dataIndex] = SIGROW.SERNUM4;
    dataIndex++;
    frame.data[dataIndex] = SIGROW.SERNUM5;
    dataIndex++;
    frame.data[dataIndex] = SIGROW.SERNUM6;
    dataIndex++;
    frame.data[dataIndex] = SIGROW.SERNUM7;
    dataIndex++;
    frame.data[dataIndex] = SIGROW.SERNUM8;
    dataIndex++;
    frame.data[dataIndex] = SIGROW.SERNUM9;
    dataIndex++;

    return (BL_HEADER + dataIndex); // total length to send back 9 byte header + payload
}

// *********************************************************************************************************************
// Request Flash Read over range using Bootloader
// [|CMD | Data Length(Low, High) Bytes to Read | unused | unused | Address(Low,High,Upper,Extended)to Start Reading From|]
// IN: [|0x01 | DataLengthL | DataLengthH | unused | unused | ADDRL | ADDRH | ADDRU | ADDRE>...]
// OUT: [|0x01 | DataLengthL | DataLengthH | unused | unused | ADDRL | ADDRH | ADDRU | ADDRE | DATA>...]
// *********************************************************************************************************************
static uint16_t BL_ReadFlash(void)
{
    flash_address_t address;
    uint16_t dataIndex;

    address = ( ( (flash_address_t)frame.address_E << 24U)
                    | ( (flash_address_t)frame.address_U << 16U)
                    | ( (flash_address_t)frame.address_H << 8U)
                    | frame.address_L);

    /*
     * Note: 
     *      This block is used to prevent read access within the boot block and outside of the normal flash range. 
     *      This was done because it is considered a security risk to allow the host to read the code out of the bootloader. 
     *   
     *      If you want to allow this type of read access in your system, you will need to delete this if block. And be warned that 
     *      a malicious host could read portions of the chips memory that should be considered private.
     */
    if(!((address >= START_OF_APP) && (address < PROGMEM_SIZE)))
    {
        frame.data[0] = ERROR_ADDRESS_OUT_OF_RANGE;
        return (10U);
    }

    // Prevent any read operation that exceeds the data buffer size
    if( frame.data_length > BL_FRAME_DATA_SIZE )
    {
        frame.data[0] = COMMAND_OVERLOAD_ERROR;
        return (10U);
    }

    for (dataIndex = 0U; dataIndex < frame.data_length; dataIndex++)
    {
        frame.data[dataIndex + 1U] = FLASH_Read(address);
        ++address;
    }
    frame.data[0] = COMMAND_SUCCESS;

    return (frame.data_length + 10U);
}

// ********************************************************************************************************************
// Request Flash Write over range, with supplied data using Bootloader
// [|CMD | Data Length(Low, High) Bytes to Write | unused | unused | Address(Low,High,Upper,Extended)to Start Writing To|]
// IN: [|0x02 | DataLengthL | DataLengthH | unused | unused | ADDRL | ADDRH | ADDRU | ADDRE | DATA>...]
// OUT: [|0x02 | DataLengthL | DataLengthH | unused | unused | ADDRL | ADDRH | ADDRU | ADDRE | STATUS_RESPONSE>...]
// ********************************************************************************************************************
static uint8_t BL_WriteFlash(void)
{
    nvm_status_t errorStatus = NVM_OK;
    flash_address_t userAddress;
    flash_address_t flashStartPageAddress;
    flash_address_t userDataStartOffset;
    flash_data_t writeBuffer[PROGMEM_PAGE_SIZE];

    userAddress = ( ( (flash_address_t)frame.address_E << 24U) 
                    | ( (flash_address_t)frame.address_U << 16U) 
                    | ( (flash_address_t)frame.address_H << 8U) 
                    | frame.address_L);
    
    if (userAddress < START_OF_APP)
    {
        frame.data[0] = ERROR_ADDRESS_OUT_OF_RANGE;
        return (10U);
    }

    // Prevent any write operation that exceeds the data buffer size
    if( frame.data_length > BL_FRAME_DATA_SIZE )
    {
        frame.data[0] = COMMAND_OVERLOAD_ERROR;
        return (10U);
    }


    // get that start of the page and the user data start address
    flashStartPageAddress = FLASH_PageAddressGet(userAddress);
    userDataStartOffset = FLASH_PageOffsetGet(userAddress);

    // read the whole page that contains the address
    for (uint16_t offset = 0U; offset < BL_FRAME_DATA_SIZE; offset++)
    {
        writeBuffer[offset] = FLASH_Read(flashStartPageAddress + offset);
    }

    for (uint16_t userByte = 0U; userByte < frame.data_length; userByte++)
    {
        writeBuffer[userDataStartOffset + userByte] = frame.data[userByte];
    }
    // ***** perform write action *****
    errorStatus = FLASH_PageErase(flashStartPageAddress);
    if (errorStatus == NVM_OK)
    {
        errorStatus = FLASH_RowWrite(flashStartPageAddress, writeBuffer);
    }

    frame.data[0] = (errorStatus == NVM_OK) ? COMMAND_SUCCESS : COMMAND_PROCESSING_ERROR;

    NVM_StatusClear();
    return (10U);
}


/************************************************************************************************
 * Erase Application Flash Space
 *        Cmd--- Length----- Keys------- Address------------------------- Data ------------------
 * In:   [|0x03 | DATALEN_L | DATALEN_L | 0x00 | 0x00 | ADDR_L | ADDR_H | ADDR_U | ADDR_E|]
 * OUT:  [|0x03 | DATALEN_L | DATALEN_L | KEY_L | KEY_H | ADDR_L | ADDR_H | ADDR_U | ADDR_E | CMD_STATUS|]
 ************************************************************************************************
 */
static uint8_t BL_EraseFlash(void)
{
    nvm_status_t errorStatus = NVM_OK;
    flash_address_t address;

    address = ( 
            ((flash_address_t)frame.address_E << 24U) 
            | ((flash_address_t)frame.address_U << 16U) 
            | ((flash_address_t)frame.address_H << 8U) 
            | frame.address_L
            );

    // Fail if the given address is not on a page boundry
    if (FLASH_PageOffsetGet(address) > (flash_address_t) 0U)
    {
        frame.data[0] = ERROR_ADDRESS_OUT_OF_RANGE;
        NVM_StatusClear();
        return (10U);
    }

    if( address != START_OF_APP ) {
        frame.data[0] = ERROR_ADDRESS_OUT_OF_RANGE;
        return (10U);
    }
    
    while (address < FLASHEND)
    {
        errorStatus = FLASH_PageErase(address);
        if (errorStatus != NVM_OK)
        {
            break;
        }
        address += PROGMEM_PAGE_SIZE;    
    }

    frame.data[0] = (errorStatus == NVM_OK) ? COMMAND_SUCCESS : COMMAND_PROCESSING_ERROR;
    NVM_StatusClear();
    return (10U);
}

/** 
 * @todo Finish the API comments here
 * @example
 * ************************************************************************************************
 * Read data from the EEPROM area of memory
 *        Cmd--- Length----- Keys------- Address------------------------- Data ------------------
 * In:   [|0x04 | DATALEN_L | DATALEN_H | 0x00 | 0x00 | ADDR_L | ADDR_H | ADDR_U | ADDR_E|]
 * OUT:  [|0x04 | DATALEN_L | DATALEN_H | KEY_L | KEY_H | ADDR_L | ADDR_H | ADDR_U | ADDR_E | CMD_STATUS | Data |.. | data |]
 * ************************************************************************************************
 */
static uint16_t BL_ReadEEData(void)
{
    eeprom_address_t address;
    address = ( ( (eeprom_address_t) frame.address_H << 8U) | frame.address_L); 

    if(!((address >= EEPROM_START_ADDRESS_U) && (address < (EEPROM_START_ADDRESS_U + EEPROM_SIZE_U))))
    {
        frame.data[0] = ERROR_ADDRESS_OUT_OF_RANGE;
        return 10U;
    }

    // Prevent any read operation that exceeds the data buffer size
    if( frame.data_length > BL_FRAME_DATA_SIZE ) {
        frame.data[0] = COMMAND_OVERLOAD_ERROR;
        return (10U);
    }

    for (uint16_t i = 0U; i < frame.data_length; i++)
    {
        frame.data[i + 1U] = EEPROM_Read(address);
        address++;
    }
    frame.data[0] = (NVM_StatusGet() == NVM_OK) ? COMMAND_SUCCESS : COMMAND_PROCESSING_ERROR;
    return (frame.data_length + 10U);
}

/** 
 * @todo Finish the API comments here
 * @example
 * ************************************************************************************************
 * Write data into the EEPROM area of memory
 *        Cmd--- Length----- Keys------- Address------------------------- Data ------------------
 * In:   [|0x05 | DATALEN_L | DATALEN_H | 0x00 | 0x00 | ADDR_L | ADDR_H | ADDR_U | ADDR_E | Data |.. | data |]
 * OUT:  [|0x05 | DATALEN_L | DATALEN_H | KEY_L | KEY_H | ADDR_L | ADDR_H | ADDR_U | ADDR_E | CMD_STATUS|]
 * ************************************************************************************************
 */
static uint8_t BL_WriteEEData(void)
{
    eeprom_address_t address;

    // Prevent any write operation that exceeds the data buffer size
    if( frame.data_length > BL_FRAME_DATA_SIZE ) {
        frame.data[0] = COMMAND_OVERLOAD_ERROR;
        return (10U);
    }

    address = ( ( (eeprom_address_t)frame.address_H << 8U) | frame.address_L);

    if(!((address >= EEPROM_START_ADDRESS_U) && (address < (EEPROM_START_ADDRESS_U + EEPROM_SIZE_U))))
    {
        frame.data[0] = ERROR_ADDRESS_OUT_OF_RANGE;
        return 10U;
    }
	
    for(uint16_t i = 0; i < frame.data_length; i++)
    {
        EEPROM_Write(address++, frame.data[i]);
        while(EEPROM_IsBusy())
        {
        }
        if(NVM_StatusGet() != NVM_OK) 
        {
            frame.data[0] = ERROR_ADDRESS_OUT_OF_RANGE;
            return (BL_HEADER + 1U);
        }
    }
    frame.data[0] = COMMAND_SUCCESS;
    return (BL_HEADER + 1U);
}

// **************************************************************************************
// Request Read device FUSE setting using bootloader
// In:    [|0x06 | DataLengthL | unused | unused | unused | ADDRL | ADDRH | ADDRU | unused |...]
// OUT:    [9 byte header + data ]
// **************************************************************************************
static uint16_t BL_ReadConfig(void)
{
    uint8_t dataIndex = 1;


    frame.data[dataIndex++] = FUSE.WDTCFG;
    frame.data[dataIndex++] = FUSE.BODCFG;
    frame.data[dataIndex++] = FUSE.OSCCFG;
    
    frame.data[dataIndex++] = FUSE.reserved_1[0];
    frame.data[dataIndex++] = FUSE.reserved_1[1];

    frame.data[dataIndex++] = FUSE.SYSCFG0;
    frame.data[dataIndex++] = FUSE.SYSCFG1;
    
    frame.data[dataIndex++] = FUSE.CODESIZE;
    frame.data[dataIndex++] = FUSE.BOOTSIZE;

    frame.data[0] = (NVM_StatusGet() == NVM_OK)? COMMAND_SUCCESS: COMMAND_PROCESSING_ERROR;
    NVM_StatusClear();
    return (BL_HEADER + dataIndex);     // 9 byte header + data 
}




// **************************************************************************************
// Reset
// In:   [|0x09 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00|]
// OUT:    [9 byte header] + [ChecksumL + ChecksumH]
// **************************************************************************************
static uint8_t BL_Reset(void)
{
    uint8_t dataIndex = 1U; 
    
    resetPending = true;
    return (BL_HEADER + dataIndex);
}
// *****************************************************************************
