/*******************************************************************************
  MPLAB Harmony Application Source File
  
  Company:
    Microchip Technology Inc.
  
  File Name:
    ccard.c

  Summary:
    This file contains the source code for the MPLAB Harmony application.

  Description:
    This file contains the source code for the MPLAB Harmony application.  It 
    implements the logic of the application's state machine and it may call 
    API routines of other MPLAB Harmony modules in the system, such as drivers,
    system services, and middleware.  However, it does not call any of the
    system interfaces (such as the "Initialize" and "Tasks" functions) of any of
    the modules in the system or make any assumptions about when those functions
    are called.  That is the responsibility of the configuration-specific system
    files.
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2013-2014 released Microchip Technology Inc.  All rights reserved.

Microchip licenses to you the right to use, modify, copy and distribute
Software only when embedded on a Microchip microcontroller or digital signal
controller that is integrated into your product or third party product
(pursuant to the sublicense terms in the accompanying license agreement).

You should refer to the license agreement accompanying this Software for
additional information regarding your rights and obligations.

SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF
MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE.
IN NO EVENT SHALL MICROCHIP OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER
CONTRACT, NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR
OTHER LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE OR
CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT OF
SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
(INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.
 *******************************************************************************/
// DOM-IGNORE-END


// *****************************************************************************
// *****************************************************************************
// Section: Included Files 
// *****************************************************************************
// *****************************************************************************

#include "ccard.h"

// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
// Taken from ccard.h

// TES relays pins
//                                    RA14 RA15  RD8  RD9 RD10 RD11  RF0  RF1  RA6  RA7  RE0  RE1
uint32_t tes_port[NUM_TES_CHANNELS] = {  0,   0,   3,   3,   3,   3,   5,   5,   0,   0,   4,   4 };
uint32_t tes_bit[NUM_TES_CHANNELS]  = { 14,  15,   8,   9,  10,  11,   0,   1,   6,   7,   0,   1 };

// Relays default state
uint32_t RELAY_DEFAULT = 0x00; 

typedef uint32_t SPI_DATA_TYPE;  
DRV_HANDLE            SPIHandle;
DRV_SPI_BUFFER_HANDLE Write_Buffer_Handle; // Write buffer handle
DRV_SPI_BUFFER_HANDLE Read_Buffer_Handle; // Read buffer handle 
SPI_DATA_TYPE         TXbuffer[6]; // SPI Driver TX buffer
SPI_DATA_TYPE         RXbuffer[6]; // SPI Driver RX buffer
uint32_t              SPI_BYTES = 4; // So far this is all that works. 
DRV_SPI_BUFFER_EVENT  test;
uint32_t              data_bits = 20; // number of data bits

// Helper functions
static inline bool cmd_read(uint32_t data)
{ 
    return(!!(data & 0x80000000));
};
// check read bit
static inline uint32_t cmd_address(uint32_t data){
    return((data & 0x7FF00000)>> 20);
};

static inline uint32_t cmd_data(uint32_t data)
{
    return(data & 0xFFFFF);
};  

static inline uint32_t make_cmd(bool read, uint32_t address, uint32_t data)
{
    return((read << 31) | ((address & (1 << (32-data_bits-1))-1) << data_bits) | (data & ((1 <<data_bits)-1)) );
};

    
// Firmware version. Coded in HEX, 1 byte per digit.
// For example: Version R2.3.1 will be 0x020301
uint32_t firmware_version = 0x010100;

// *****************************************************************************
// Taken from ccard.c
uint32_t relay; 
uint32_t response; // return from card for testing
uint32_t cycle_count=0;
uint32_t last_cycle_count = 0;
uint32_t count_increment = 100000;
uint32_t command_count = 0;

uint32_t command; // command from controller
uint32_t addr;
uint32_t default_addr = 0x00; // used until set to some specific value,
uint32_t data;
bool rd;  // read / write data

#define num_test_commands 100
uint32_t CMD[num_test_commands];
uint32_t status = 0; 
uint32_t return_data; 
uint32_t *regptr[ADDR_COUNT];  // will hold pointers to various registers

uint32_t adc_data[ADC_CHAN_COUNT*ADC_CHAN_SAMPLE_COUNT + 1];
uint32_t hemt_bias;
uint32_t a50k_bias;
uint32_t temperature;
uint32_t id_volt;

uint32_t n;

uint32_t ps_en;             // Power supplies (HEMT and 50k) enable
uint32_t flux_ramp_control; // Flux ramp (voltage and current mode) controls

// *****************************************************************************
/* Application Data

  Summary:
    Holds application data

  Description:
    This structure holds the application's data.

  Remarks:
    This structure should be initialized by the APP_Initialize function.
    
    Application strings and buffers are be defined outside this structure.
*/

CCARD_DATA ccardData;

// *****************************************************************************
// *****************************************************************************
// Section: Application Callback Functions
// *****************************************************************************
// *****************************************************************************

/* TODO:  Add any necessary callback functions.
*/

// *****************************************************************************
// *****************************************************************************
// Section: Application Local Functions
// *****************************************************************************
// *****************************************************************************


/* TODO:  Add any necessary local functions.
*/


// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    void CCARD_Initialize ( void )

  Remarks:
    See prototype in ccard.h.
 */

void CCARD_Initialize ( void )
{
    /* Place the App state machine in its initial state. */
    ccardData.state = CCARD_STATE_INIT;

    
    TXbuffer[0] = 0;
}


/******************************************************************************
  Function:
    void CCARD_Tasks ( void )

  Remarks:
    See prototype in ccard.h.
 */

void CCARD_Tasks ( void )
{

    /* Check the application's current state. */
    switch ( ccardData.state )
    {
        /* Application's initial state. */
        case CCARD_STATE_INIT:
        {
            cycle_count = 0;
            relay = RELAY_DEFAULT; // clear all relays
            TES_relay_set(relay); // set relays to default

            // Disable power supplies
            PS_HEMT_ENOff();
            PS_50k_ENOff();

            // Set flux ramps to DC coupled state
            FluxRampVoltModeOff();
            FluxRampCurrModeOff();

            // set up register map
            regptr[ADDR_VERSION]      = &firmware_version;
            regptr[ADDR_STATUS]       = &status;
            regptr[ADDR_RELAY]        = &relay;
            regptr[ADDR_HEMT_BIAS]    = &hemt_bias;
            regptr[ADDR_50K_BIAS]     = &a50k_bias;
            regptr[ADDR_TEMPERATURE]  = &temperature;
            regptr[ADDR_COUNTER]      = &cycle_count;
            regptr[ADDR_PS_EN]        = &ps_en;
            regptr[ADDR_FLUX_RAMP]    = &flux_ramp_control;
            regptr[ADDR_ID_VOLT]      = &id_volt;

            // this is the SPI used for receiving commands
            SPIHandle = DRV_SPI_Open(DRV_SPI_INDEX_0, DRV_IO_INTENT_READWRITE );

            // read buffer
            Read_Buffer_Handle = DRV_SPI_BufferAddWriteRead(
                    SPIHandle,
                    (SPI_DATA_TYPE *)& TXbuffer[0],
                    SPI_BYTES,
                    (SPI_DATA_TYPE *)& RXbuffer[0],
                    SPI_BYTES,
                    0,
                    0);

            DRV_ADC_Open();
            DRV_ADC_Start(); // start ADC running

            ccardData.state = CCARD_STATE_SERVICE_TASKS;
            
            break;
        }

        case CCARD_STATE_SERVICE_TASKS:
        {
            if (DRV_SPI_BUFFER_EVENT_COMPLETE & DRV_SPI_BufferStatus(Read_Buffer_Handle)) // check for SPI data
            {
                ccardData.state = CCARD_READ_SPI;  // need to read SPI data
                break;
            }
            
            if (DRV_ADC_SamplesAvailable())
            {
               ccardData.state = CCARD_READ_ADC;
                break;
            }
            
            break;
        }

        /* TODO: implement your application state machine.*/
        case CCARD_READ_SPI:  // this is where we receive commands
        {
            cycle_count++;
            command         = RXbuffer[0]; // lock in command for decoding
            rd              = cmd_read(command);
            addr            = cmd_address(command);
            data            = cmd_data(command);  
            ccardData.state = CCARD_STATE_SERVICE_TASKS;   // may be overridden later
            
            if (rd)
            {                
                // Read command received.
                
                if (addr < ADDR_COUNT)  // address in range
                {
                    TXbuffer[0] = make_cmd(0,  addr, *regptr[addr] );
                    //TXbuffer[0] = 0x01;
                    default_addr = addr; // this is now the default read back. 
                } 
                else
                {
                    TXbuffer[0] = make_cmd(0, default_addr, *regptr[default_addr]);
                }
                
                // start new data read
                Read_Buffer_Handle = DRV_SPI_BufferAddWriteRead(
                        SPIHandle,
                        (SPI_DATA_TYPE *)& TXbuffer[0], 
                        SPI_BYTES, 
                        (SPI_DATA_TYPE *)& RXbuffer[0], 
                        SPI_BYTES,
                        0,
                        0);
            }
            else
            { 
                // Write command received.
                
                TXbuffer[0] = make_cmd(0, default_addr, *regptr[default_addr]);
                //TXbuffer[0] = 0x01;
                
                switch (addr)
                {
                    case ADDR_RELAY:
                    {
                        relay = data | (1 << (data_bits -1)); // set bit to show that relays are in motion
                        TES_relay_set(relay); // set relays to default
                        break;
                    }
                    case ADDR_PS_EN:
                    {
                        // Only 2 bits are used
                        ps_en = data & 0x03;

                        // HEMT_EN (bit 0)
                        PS_HEMT_ENStateSet( ps_en & 0x01 );

                        // 50k_EN (bit 1)
                        PS_50k_ENStateSet( ( ps_en >> 1 ) & 0x01 );

                        break;
                    }
                    case ADDR_FLUX_RAMP:
                    {
                        // Only 2 bits are used
                        flux_ramp_control = data & 0x03;

                        // Voltage mode flux ramp control (bit 0)
                        FluxRampVoltModeStateSet( flux_ramp_control & 0x01 );

                        // Current mode flux ramp control (bit 1)
                        FluxRampCurrModeStateSet( ( flux_ramp_control >> 1 ) & 0x01 );

                        break;
                    }
                    default:
                    {
                        break;
                    }    
                }
                Read_Buffer_Handle = DRV_SPI_BufferAddWriteRead(
                        SPIHandle, 
                        (SPI_DATA_TYPE *)& TXbuffer[0], 
                        SPI_BYTES, 
                        (SPI_DATA_TYPE *)& RXbuffer[0], 
                        SPI_BYTES,
                        0,
                        0); // start new data read
            }
            break;
        }
        
        case CCARD_READ_ADC:
        {
            DRV_ADC_Stop();
            
            // KLUDGE< first points seem bad!
            for ( n = 0;
                  n < ADC_CHAN_COUNT*ADC_CHAN_SAMPLE_COUNT + 1;
                  n++)
            { 
                adc_data[n] = DRV_ADC_SamplesRead(n);
            }  
            
            // start ADC running again
            DRV_ADC_Start();
            
            // Average 'ADC_CHAN_SAMPLE_COUNT' samples per channel. The average 
            // is done in software, here we just accumulate the samples.
            hemt_bias   = 0;
            a50k_bias   = 0;
            temperature = 0;
            id_volt     = 0;
            
            for ( n = ADC_HEMT_BIAS_CHAN;
                  n < ADC_CHAN_COUNT*ADC_CHAN_SAMPLE_COUNT;
                  n += ADC_CHAN_COUNT)
            {
                hemt_bias += adc_data[n];
            }
            
            for ( n = ADC_50K_BIAS_CHAN;
                  n < ADC_CHAN_COUNT*ADC_CHAN_SAMPLE_COUNT;
                  n += ADC_CHAN_COUNT)
            {
                a50k_bias += adc_data[n];
            }
            
            for ( n = ADC_TEMPERATURE_CHAN;
                  n < ADC_CHAN_COUNT*ADC_CHAN_SAMPLE_COUNT;
                  n += ADC_CHAN_COUNT)
            {
                temperature += adc_data[n];
            }
            
            for ( n = ADC_ID_VOLT_CHAN;
                  n < ADC_CHAN_COUNT*ADC_CHAN_SAMPLE_COUNT;
                  n += ADC_CHAN_COUNT)
            {
                id_volt += adc_data[n];
            }

            ccardData.state = CCARD_STATE_SERVICE_TASKS;
            break;
         }   

        /* The default state should never be executed. */
        default:
        {
            /* TODO: Handle error in application's state machine. */
            break;
        }
    }
}

 

/*******************************************************************************
 End of File
 */
