#ifndef __USB_ROUTINE_H 
#define __USB_ROUTINE_H

#include <stdint.h>

#define DISPLAY_MODE_VIDEO 3

// 128x64 monochrome SSD1306 frame buffer size (8 pages * 128 columns)
#define VIDEO_FRAME_BYTES 1024


extern uint8_t LED_flag;
extern uint8_t LED_status;
extern uint8_t current_display_mode;

extern volatile uint16_t Bulk_Out_Len;

/* Mode switch control transfer variables (replaces old EP3 command mechanism) */
extern volatile uint8_t USBHS_Mode_Switch_Flag;
extern volatile uint8_t USBHS_Mode_Switch_Value;


/******************************************************************************/
/* external functions */
void USB_command_check(void);
void USB_bulk_data_handler(void);

#endif
