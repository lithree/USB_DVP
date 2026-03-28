#include "ch32v30x_usbhs_device.h"
#include "debug.h"
#include "ov.h"
#include "usb_routine.h"
#include "string.h"

#define IMG_WIDTH  OV2640_RGB565_WIDTH
#define IMG_HEIGHT OV2640_RGB565_HEIGHT
#define LINE_BYTES (IMG_WIDTH * 2)
#define DVP_LINE_BUFFER_COUNT 8
#define ROW_HDR_BYTES 7
#define ROW_PACKET_BYTES (ROW_HDR_BYTES + LINE_BYTES)

extern __attribute__((aligned(4))) uint8_t image_buffer[LINE_BYTES * DVP_LINE_BUFFER_COUNT];
extern volatile uint8_t frame_capture_done;
extern void DVP_ResetCaptureState(void);
extern volatile uint32_t frame_cnt;
extern volatile uint32_t addr_cnt;
extern volatile uint32_t href_cnt;
extern volatile uint32_t dvp_fifo_overflow_cnt;
extern volatile uint32_t usb_rows_sent;

/* Variables used for the USB transmission state machine */
static uint8_t usb_is_sending = 0;
static uint32_t usb_tx_row = 0;
static uint8_t usb_tx_frame_id = 0;

extern volatile uint32_t dvp_frame_done_cnt;

static void restart_dvp_capture(void)
{
    DVP->CR0 &= ~RB_DVP_ENABLE;
    DVP_ResetCaptureState();
    DVP->CR0 |= RB_DVP_ENABLE;
}

static void process_host_command(const uint8_t *packet, uint16_t length)
{
    if (length == 0)
    {
        return;
    }

    switch (packet[0])
    {
        case 0x01:
            USBHS_Endp_Busy[DEF_UEP4] = DEF_UEP_FREE;
            USBHSD->UEP4_TX_CTRL = (USBHSD->UEP4_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
            usb_is_sending = 1;
            usb_tx_row = 0;
            usb_tx_frame_id = 0;
            usb_rows_sent = 0;
            frame_capture_done = 0;

            restart_dvp_capture();
            break;

        case 0x02:
            usb_is_sending = 0;
            usb_tx_row = 0;
            usb_tx_frame_id = 0;
            usb_rows_sent = 0;
            frame_capture_done = 0;
            DVP->CR0 &= ~RB_DVP_ENABLE;
            break;

        default:
            break;
    }
}

static void process_ep3_command_if_pending(void)
{
    uint8_t cmd_byte = 0;
    uint16_t cmd_len = 0;

    NVIC_DisableIRQ(USBHS_IRQn);
    cmd_len = USBHS_EP3_Rx_Len;
    if (cmd_len > 0U)
    {
        cmd_byte = USBHS_EP3_Rx_Buf[0];
        USBHS_EP3_Rx_Len = 0;
        USBHSD->UEP3_RX_CTRL = (USBHSD->UEP3_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
    }
    NVIC_EnableIRQ(USBHS_IRQn);

    if (cmd_len > 0U)
    {
        process_host_command(&cmd_byte, 1);
    }
}

void USB_command_check(void)
{
    /* Parsing of host computer control commands can be implemented here */
}

void USB_bulk_data_handler(void)
{
    if (!USBHS_DevEnumStatus)
    {
        return;
    }

    /* EP3 (Bulk OUT / Rx): Process command byte and re-arm EP3 RX ACK. */
    process_ep3_command_if_pending();

    /* EP1 (Bulk OUT / Rx): Process data ring buffer if this endpoint is used. */
    while (RingBuffer_Comm.RemainPack > 0)
    {
        uint8_t *packet = &Data_Buffer[RingBuffer_Comm.DealPtr * DEF_USBD_HS_PACK_SIZE];
        uint16_t packet_len = RingBuffer_Comm.PackLen[RingBuffer_Comm.DealPtr];

        process_host_command(packet, packet_len);

        NVIC_DisableIRQ(USBHS_IRQn);
        RingBuffer_Comm.RemainPack--;
        RingBuffer_Comm.DealPtr++;
        if (RingBuffer_Comm.DealPtr == DEF_Ring_Buffer_Max_Blks)
        {
            RingBuffer_Comm.DealPtr = 0;
        }

        if (RingBuffer_Comm.RemainPack < (DEF_Ring_Buffer_Max_Blks - DEF_RING_BUFFER_RESTART))
        {
            if (RingBuffer_Comm.StopFlag)
            {
                RingBuffer_Comm.StopFlag = 0;
                /* Restore EP3 RX ACK state to receive new packets */
                USBHSD->UEP3_RX_CTRL = (USBHSD->UEP3_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
            }
        }
        NVIC_EnableIRQ(USBHS_IRQn);
    }

    /* EP4 (Bulk IN / Tx): Send DVP image data to the host */
    if (usb_is_sending == 1)
    {
        /* Check EP4 TX status */
        if ((USBHSD->UEP4_TX_CTRL & USBHS_UEP_T_RES_MASK) != USBHS_UEP_T_RES_ACK)
        {
            /* Send one row at a time from the 8-line DVP ring buffer. */
            if (usb_tx_row < addr_cnt)
            {
                uint32_t slot = usb_tx_row % DVP_LINE_BUFFER_COUNT;
                uint16_t row_index = (uint16_t)usb_tx_row;
                uint8_t *row_ptr = &image_buffer[slot * LINE_BYTES];

                USBHS_EP4_Tx_Buf[0] = 'O';
                USBHS_EP4_Tx_Buf[1] = 'V';
                USBHS_EP4_Tx_Buf[2] = '2';
                USBHS_EP4_Tx_Buf[3] = '6';
                USBHS_EP4_Tx_Buf[4] = usb_tx_frame_id;
                USBHS_EP4_Tx_Buf[5] = (uint8_t)(row_index & 0xFFU);
                USBHS_EP4_Tx_Buf[6] = (uint8_t)(row_index >> 8);
                memcpy(&USBHS_EP4_Tx_Buf[ROW_HDR_BYTES], row_ptr, LINE_BYTES);

                USBHSD->UEP4_TX_LEN = ROW_PACKET_BYTES;
                USBHSD->UEP4_TX_CTRL = (USBHSD->UEP4_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;

                usb_tx_row++;
                usb_rows_sent = usb_tx_row;

                /* Resume DVP capture once at least one line slot is free. */
                if ((frame_capture_done == 0U) && ((addr_cnt - usb_rows_sent) < DVP_LINE_BUFFER_COUNT) &&
                    ((DVP->CR0 & RB_DVP_ENABLE) == 0U) && (addr_cnt < IMG_HEIGHT))
                {
                    DVP->CR0 |= RB_DVP_ENABLE;
                }
            }
            else if ((frame_capture_done != 0U) && (usb_tx_row >= IMG_HEIGHT))
            {
                usb_tx_row = 0;
                usb_tx_frame_id++;
                usb_rows_sent = 0;
                frame_capture_done = 0;
                restart_dvp_capture();
            }
        }
    }
}