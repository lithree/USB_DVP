#ifndef __USB_ROUTINE_H 
#define __USB_ROUTINE_H

#include <stdint.h>

#define IMG_WIDTH  640
#define IMG_HEIGHT 480
#define LINE_BYTES (IMG_WIDTH * 2)
#define VIDEO_FRAME_BYTES (IMG_WIDTH * IMG_HEIGHT * 2)
#define VIDEO_PACKET_HEADER_BYTES 9
#define VIDEO_PACKET_PAYLOAD_BYTES 503

#define DISPLAY_MODE_VIDEO 3
#define USB_STREAM_CMD_START 0x01
#define USB_STREAM_CMD_STOP  0x02

extern uint8_t LED_flag;
extern uint8_t LED_status;

extern volatile uint8_t USBHS_Mode_Switch_Flag;
extern volatile uint8_t USBHS_Mode_Switch_Value;


void USB_command_check(void);
void USB_bulk_data_handler(void);

#endif
