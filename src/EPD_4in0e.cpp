/*****************************************************************************
* | File        :   EPD_4in0e.c
* | Author      :   Waveshare team
* | Function    :   4inch e-Paper (E) Driver
* | Info        :
*----------------
* | This version:   V1.0
* | Date        :   2024-08-20
* | Info        :
* -----------------------------------------------------------------------------
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#include "EPD_4in0e.h"
#include "Debug.h"
#include <LittleFS.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <Arduino.h>

/******************************************************************************
function :  Software reset
parameter:
******************************************************************************/
static void EPD_4IN0E_Reset(void)
{
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
    DEV_Digital_Write(EPD_RST_PIN, 0);
    DEV_Delay_ms(2);
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
}

/******************************************************************************
function :  send command
parameter:
     Reg : Command register
******************************************************************************/
static void EPD_4IN0E_SendCommand(UBYTE Reg)
{
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Reg);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :  send data
parameter:
    Data : Write data
******************************************************************************/
static void EPD_4IN0E_SendData(UBYTE Data)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Data);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

/******************************************************************************
function :  Wait until the busy_pin goes HIGH (idle) using light sleep
parameter:
    BUSY pin: LOW = busy (display updating), HIGH = idle (display done)
    Uses light sleep with GPIO wake to save power during long waits
******************************************************************************/
static void EPD_4IN0E_ReadBusyH(void)
{
    Debug("e-Paper busy H\r\n");
    
    // Check if BUSY is already HIGH (display done)
    if (DEV_Digital_Read(EPD_BUSY_PIN) == HIGH) {
        DEV_Delay_ms(200);
        Debug("e-Paper busy H release\r\n");
        return;
    }
    
    // BUSY is LOW - display is updating
    // Use light sleep with GPIO wake to save power
    // ESP32-C3: Wake when BUSY pin goes HIGH (display done)
    
    unsigned long timeout = millis() + 60000; // 60 second timeout (safety)
    bool displayDone = false;
    
    while (!displayDone && (millis() < timeout)) {
        // Check current state before entering sleep
        if (DEV_Digital_Read(EPD_BUSY_PIN) == HIGH) {
            displayDone = true;
            break;
        }
        
        // Configure GPIO wakeup for BUSY pin (HIGH level = display done)
        // ESP32-C3: Use gpio_wakeup_enable for light sleep
        gpio_wakeup_enable((gpio_num_t)EPD_BUSY_PIN, GPIO_INTR_HIGH_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        
        // Enter light sleep - will wake when BUSY goes HIGH or timeout
        esp_light_sleep_start();
        
        // Check wake reason
        esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
        if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
            // Woke on GPIO - check if BUSY is now HIGH
            if (DEV_Digital_Read(EPD_BUSY_PIN) == HIGH) {
                displayDone = true;
            }
        }
        
        // Disable wakeup (will re-enable if we loop again)
        gpio_wakeup_disable((gpio_num_t)EPD_BUSY_PIN);
        
        // Check timeout
        if (millis() >= timeout) {
            Debug("e-Paper busy timeout!\r\n");
            break;
        }
    }
    
    // Small delay to ensure display is fully ready
    DEV_Delay_ms(200);
    Debug("e-Paper busy H release\r\n");
}

/******************************************************************************
function :  Turn On Display
parameter:
******************************************************************************/
static void EPD_4IN0E_TurnOnDisplay(void)
{
    
    EPD_4IN0E_SendCommand(0x04); // POWER_ON
    EPD_4IN0E_ReadBusyH();
    DEV_Delay_ms(200);

    //Second setting 
    EPD_4IN0E_SendCommand(0x06);
    EPD_4IN0E_SendData(0x6F);
    EPD_4IN0E_SendData(0x1F);
    EPD_4IN0E_SendData(0x17);
    EPD_4IN0E_SendData(0x27);
    DEV_Delay_ms(200);

    EPD_4IN0E_SendCommand(0x12); // DISPLAY_REFRESH
    EPD_4IN0E_SendData(0x00);
    EPD_4IN0E_ReadBusyH();

    EPD_4IN0E_SendCommand(0x02); // POWER_OFF
    EPD_4IN0E_SendData(0X00);
    EPD_4IN0E_ReadBusyH();
    DEV_Delay_ms(200);
}

/******************************************************************************
function :  Initialize the e-Paper register
parameter:
******************************************************************************/
void EPD_4IN0E_Init(void)
{
    EPD_4IN0E_Reset();
    EPD_4IN0E_ReadBusyH();
    DEV_Delay_ms(30);

    EPD_4IN0E_SendCommand(0xAA);    // CMDH
    EPD_4IN0E_SendData(0x49);
    EPD_4IN0E_SendData(0x55);
    EPD_4IN0E_SendData(0x20);
    EPD_4IN0E_SendData(0x08);
    EPD_4IN0E_SendData(0x09);
    EPD_4IN0E_SendData(0x18);

    EPD_4IN0E_SendCommand(0x01);
    EPD_4IN0E_SendData(0x3F);

    EPD_4IN0E_SendCommand(0x00);
    EPD_4IN0E_SendData(0x5F);
    EPD_4IN0E_SendData(0x69);

    EPD_4IN0E_SendCommand(0x05);
    EPD_4IN0E_SendData(0x40);
    EPD_4IN0E_SendData(0x1F);
    EPD_4IN0E_SendData(0x1F);
    EPD_4IN0E_SendData(0x2C);

    EPD_4IN0E_SendCommand(0x08);
    EPD_4IN0E_SendData(0x6F);
    EPD_4IN0E_SendData(0x1F);
    EPD_4IN0E_SendData(0x1F);
    EPD_4IN0E_SendData(0x22);

    EPD_4IN0E_SendCommand(0x06);
    EPD_4IN0E_SendData(0x6F);
    EPD_4IN0E_SendData(0x1F);
    EPD_4IN0E_SendData(0x17);
    EPD_4IN0E_SendData(0x17);

    EPD_4IN0E_SendCommand(0x03);
    EPD_4IN0E_SendData(0x00);
    EPD_4IN0E_SendData(0x54);
    EPD_4IN0E_SendData(0x00);
    EPD_4IN0E_SendData(0x44); 

    EPD_4IN0E_SendCommand(0x60);
    EPD_4IN0E_SendData(0x02);
    EPD_4IN0E_SendData(0x00);

    EPD_4IN0E_SendCommand(0x30);
    EPD_4IN0E_SendData(0x08);

    EPD_4IN0E_SendCommand(0x50);
    EPD_4IN0E_SendData(0x3F);

    EPD_4IN0E_SendCommand(0x61);
    EPD_4IN0E_SendData(0x01);
    EPD_4IN0E_SendData(0x90);
    EPD_4IN0E_SendData(0x02); 
    EPD_4IN0E_SendData(0x58);

    EPD_4IN0E_SendCommand(0xE3);
    EPD_4IN0E_SendData(0x2F);

    EPD_4IN0E_SendCommand(0x84);
    EPD_4IN0E_SendData(0x01);
    EPD_4IN0E_ReadBusyH();

}

/******************************************************************************
function :  Clear screen
parameter:
******************************************************************************/
void EPD_4IN0E_Clear(UBYTE color)
{
    UWORD Width, Height;
    Width = (EPD_4IN0E_WIDTH % 2 == 0)? (EPD_4IN0E_WIDTH / 2 ): (EPD_4IN0E_WIDTH / 2 + 1);
    Height = EPD_4IN0E_HEIGHT;

    EPD_4IN0E_SendCommand(0x10);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_4IN0E_SendData((color<<4)|color);
        }
    }

    EPD_4IN0E_TurnOnDisplay();
}

/******************************************************************************
function :  show 7 kind of color block
parameter:
******************************************************************************/
void EPD_4IN0E_Show7Block(void)
{
    unsigned long j, k;
    unsigned char const Color_seven[6] = 
    {EPD_4IN0E_BLACK, EPD_4IN0E_YELLOW, EPD_4IN0E_RED, EPD_4IN0E_BLUE, EPD_4IN0E_GREEN, EPD_4IN0E_WHITE};

    EPD_4IN0E_SendCommand(0x10);
    for(k = 0 ; k < 6; k ++) {
        for(j = 0 ; j < 20000; j ++) {
            EPD_4IN0E_SendData((Color_seven[k]<<4) |Color_seven[k]);
        }
    }
    EPD_4IN0E_TurnOnDisplay();
}

void EPD_4IN0E_Show(void)
{
    unsigned long k,o;
    unsigned char const Color_seven[6] = 
    {EPD_4IN0E_BLACK, EPD_4IN0E_YELLOW, EPD_4IN0E_RED, EPD_4IN0E_BLUE, EPD_4IN0E_GREEN, EPD_4IN0E_WHITE};

    UWORD Width, Height;
    Width = (EPD_4IN0E_WIDTH % 2 == 0)? (EPD_4IN0E_WIDTH / 2 ): (EPD_4IN0E_WIDTH / 2 + 1);
    Height = EPD_4IN0E_HEIGHT;
    k = 0;
    o = 0;

    EPD_4IN0E_SendCommand(0x10);
    for (UWORD j = 0; j < Height; j++) {
        if((j > 10) && (j<50))
        for (UWORD i = 0; i < Width; i++) {
                EPD_4IN0E_SendData((Color_seven[0]<<4) |Color_seven[0]);
            }
        else if(o < Height/2)
        for (UWORD i = 0; i < Width; i++) {
                EPD_4IN0E_SendData((Color_seven[0]<<4) |Color_seven[0]);
            }
        
        else
        {
            for (UWORD i = 0; i < Width; i++) {
                EPD_4IN0E_SendData((Color_seven[k]<<4) |Color_seven[k]);
                
            }
            k++ ;
            if(k >= 6)
                k = 0;
        }
            
        o++ ;
        if(o >= Height)
            o = 0;
    }
    EPD_4IN0E_TurnOnDisplay();
}

/******************************************************************************
function :  Sends the image buffer in RAM to e-Paper and displays
parameter:
******************************************************************************/
void EPD_4IN0E_Display(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_4IN0E_WIDTH % 2 == 0)? (EPD_4IN0E_WIDTH / 2 ): (EPD_4IN0E_WIDTH / 2 + 1);
    Height = EPD_4IN0E_HEIGHT;

    EPD_4IN0E_SendCommand(0x10);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_4IN0E_SendData(Image[i + j * Width]);
        }
    }
    EPD_4IN0E_TurnOnDisplay();
}

/******************************************************************************
function :  Stream image data from file directly to e-Paper display
parameter:
    file : File object opened for reading
    imageSize : Expected size of image data in bytes
returns: true if successful, false on error
******************************************************************************/
bool EPD_4IN0E_DisplayFromFile(File &file, size_t imageSize)
{
    if (!file) {
        return false;
    }
    
    if (file.size() != imageSize) {
        return false;
    }
    
    UWORD Width, Height;
    Width = (EPD_4IN0E_WIDTH % 2 == 0)? (EPD_4IN0E_WIDTH / 2 ): (EPD_4IN0E_WIDTH / 2 + 1);
    Height = EPD_4IN0E_HEIGHT;
    
    // Verify expected size matches display dimensions
    size_t expectedSize = Width * Height;
    if (imageSize != expectedSize) {
        return false;
    }
    
    EPD_4IN0E_SendCommand(0x10);
    
    size_t totalBytesRead = 0;
    
    // Read and send data row by row
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            // Read one byte at a time to match the original display logic
            if (file.available() > 0) {
                int byteRead = file.read();
                if (byteRead == -1) {
                    return false;  // Read error
                }
                EPD_4IN0E_SendData((UBYTE)byteRead);
                totalBytesRead++;
            } else {
                return false;  // Unexpected EOF
            }
        }
    }
    
    // Verify we read exactly the expected amount
    if (totalBytesRead != imageSize) {
        return false;
    }
    
    EPD_4IN0E_TurnOnDisplay();
    return true;
}

void EPD_4IN0E_DisplayPart(const UBYTE *Image, UWORD xstart, UWORD ystart, UWORD image_width, UWORD image_heigh)
{
	unsigned long i, j;
	UWORD Width, Height;
	Width = (EPD_4IN0E_WIDTH % 2 == 0)? (EPD_4IN0E_WIDTH / 2 ): (EPD_4IN0E_WIDTH / 2 + 1);
	Height = EPD_4IN0E_HEIGHT;
	
	EPD_4IN0E_SendCommand(0x10);
	for(i=0; i<Height; i++) {
		for(j=0; j<Width; j++) {
			if((i<(image_heigh+ystart)) && (i>=ystart) && (j<((image_width+xstart)/2)) && (j>=(xstart/2))) {
				EPD_4IN0E_SendData(Image[(j-xstart/2) + (image_width/2*(i-ystart))]);
			}
			else {
				EPD_4IN0E_SendData(0x11);
			}
		}
	}
	EPD_4IN0E_TurnOnDisplay();
}

/******************************************************************************
function :  Enter sleep mode
parameter:
******************************************************************************/
void EPD_4IN0E_Sleep(void)
{
    EPD_4IN0E_SendCommand(0x07); // DEEP_SLEEP
    EPD_4IN0E_SendData(0XA5);
    // EPD_4IN0E_ReadBusyH();
}

