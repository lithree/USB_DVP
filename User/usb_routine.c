#include "ch32v30x_usbhs_device.h"
#include "debug.h"
#include "usb_routine.h"
#include "string.h"

#define DVP_LINE_BUFFER_COUNT 8

extern __attribute__((aligned(4))) uint8_t image_buffer[LINE_BYTES * DVP_LINE_BUFFER_COUNT];
extern volatile uint8_t frame_capture_done;
extern void DVP_ResetCaptureState(void);
extern void UART6_Printf(char *format, ...);
extern volatile uint32_t frame_cnt;
extern volatile uint32_t addr_cnt;
extern volatile uint32_t href_cnt;
extern volatile uint32_t dvp_fifo_overflow_cnt;
extern volatile uint32_t usb_rows_sent;

static uint8_t usb_is_sending = 0;
static uint32_t usb_tx_row = 0;
static uint16_t usb_tx_row_offset = 0;
static uint8_t usb_tx_frame_id = 0;
static uint32_t usb_tx_packet_count = 0;
static uint32_t usb_tx_frame_complete_count = 0;
static uint32_t usb_tx_short_packet_count = 0;
static uint32_t usb_ep3_cmd_count = 0;
static uint8_t usb_row_debug_chunk_count = 0;
static uint8_t usb_row_debug_enabled = 1;

extern volatile uint32_t dvp_frame_done_cnt;

static void restart_dvp_capture(void)
{
    DVP->CR0 &= ~RB_DVP_ENABLE;
    DVP_ResetCaptureState();
    DVP->CR0 |= RB_DVP_ENABLE;
}

static void start_streaming_session(uint8_t new_frame_id)
{
    USBHS_Endp_Busy[DEF_UEP4] = DEF_UEP_FREE;
    USBHSD->UEP4_TX_CTRL = (USBHSD->UEP4_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
    usb_is_sending = 1;
    usb_tx_row = 0;
    usb_tx_row_offset = 0;
    usb_tx_frame_id = new_frame_id;
    usb_rows_sent = 0;
    frame_capture_done = 0;
    restart_dvp_capture();
}

static void process_host_command(const uint8_t *packet, uint16_t length)
{
    if (length == 0)
    {
        return;
    }

    UART6_Printf("[usb] ep3 cmd=0x%02x len=%u enum=%u ep3_out=%lu\r\n",
                 packet[0],
                 (unsigned int)length,
                 (unsigned int)USBHS_DevEnumStatus,
                 (unsigned long)USBHS_EP3_Out_Cnt);
    usb_ep3_cmd_count++;

    switch (packet[0])
    {
        case USB_STREAM_CMD_START:
            start_streaming_session((uint8_t)(usb_tx_frame_id + 1U));
            usb_row_debug_chunk_count = 0;
            UART6_Printf("[usb] stream start rows=%lu frame_done=%u\r\n",
                         (unsigned long)addr_cnt,
                         (unsigned int)frame_capture_done);
            break;

        case USB_STREAM_CMD_STOP:
            usb_is_sending = 0;
            usb_tx_row = 0;
            usb_tx_row_offset = 0;
            usb_rows_sent = 0;
            frame_capture_done = 0;
            DVP->CR0 &= ~RB_DVP_ENABLE;
            UART6_Printf("[usb] stream stop\r\n");
            break;

        default:
            UART6_Printf("[usb] unknown ep3 cmd=0x%02x\r\n", packet[0]);
            break;
    }
}

void USB_command_check(void)
{
    if (USBHS_Mode_Switch_Flag != 0U)
    {
        uint8_t mode = USBHS_Mode_Switch_Value;

        USBHS_Mode_Switch_Flag = 0;
        UART6_Printf("[usb] mode switch=%u\r\n", (unsigned int)mode);
    }
}

static void process_ep3_command_if_pending(void)
{
    uint16_t cmd_len;

    NVIC_DisableIRQ(USBHS_IRQn);
    cmd_len = USBHS_EP3_Rx_Len;
    if (cmd_len > 0U)
    {
        USBHS_EP3_Rx_Len = 0U;
        USBHSD->UEP3_RX_CTRL = (USBHSD->UEP3_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
    }
    NVIC_EnableIRQ(USBHS_IRQn);

    if (cmd_len > 0U)
    {
        process_host_command(USBHS_EP3_Rx_Buf, cmd_len);
    }
}

void USB_bulk_data_handler(void)
{
    if (!USBHS_DevEnumStatus)
    {
        return;
    }

    USB_command_check();

    /* EP3 (Bulk OUT / Rx): Process command bytes received from the host. */
    process_ep3_command_if_pending();

    /* EP1 (Bulk OUT / Rx): Drain ring-buffer payloads if this endpoint is used. */
    while (RingBuffer_Comm.RemainPack > 0)
    {
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
                /* Restore EP1 RX ACK state to receive new packets */
                USBHSD->UEP1_RX_CTRL = (USBHSD->UEP1_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
            }
        }
        NVIC_EnableIRQ(USBHS_IRQn);
    }

    /* EP4 BULK */
    if (usb_is_sending == 1)
    {
        /* Check EP4 TX status */
        if ((USBHSD->UEP4_TX_CTRL & USBHS_UEP_T_RES_MASK) != USBHS_UEP_T_RES_ACK)
        {
            if (usb_tx_row < addr_cnt)
            {
                uint32_t slot = usb_tx_row % DVP_LINE_BUFFER_COUNT;
                uint8_t *row_ptr = &image_buffer[slot * LINE_BYTES];
                uint16_t remain = (uint16_t)(LINE_BYTES - usb_tx_row_offset);
                uint16_t tx_len = remain;

                if (tx_len > VIDEO_PACKET_PAYLOAD_BYTES)
                {
                    tx_len = VIDEO_PACKET_PAYLOAD_BYTES;
                }

                /* Prefix each payload chunk so the host can resync. */
                USBHS_EP4_Tx_Buf[0] = 'O';
                USBHS_EP4_Tx_Buf[1] = 'V';
                USBHS_EP4_Tx_Buf[2] = '2';
                USBHS_EP4_Tx_Buf[3] = '6';
                USBHS_EP4_Tx_Buf[4] = usb_tx_frame_id;
                USBHS_EP4_Tx_Buf[5] = (uint8_t)(usb_tx_row & 0xFFU);
                USBHS_EP4_Tx_Buf[6] = (uint8_t)((usb_tx_row >> 8) & 0xFFU);
                USBHS_EP4_Tx_Buf[7] = (uint8_t)(usb_tx_row_offset & 0xFFU);
                USBHS_EP4_Tx_Buf[8] = (uint8_t)((usb_tx_row_offset >> 8) & 0xFFU);
                memcpy(&USBHS_EP4_Tx_Buf[VIDEO_PACKET_HEADER_BYTES], row_ptr + usb_tx_row_offset, tx_len);

                USBHSD->UEP4_TX_LEN = (uint16_t)(VIDEO_PACKET_HEADER_BYTES + tx_len);
                USBHSD->UEP4_TX_CTRL = (USBHSD->UEP4_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
                usb_tx_packet_count++;
                if ((usb_row_debug_enabled != 0U) && (usb_tx_row < 3U) && (usb_row_debug_chunk_count < 9U))
                {
                    UART6_Printf("[usb] rowchunk fid=%u row=%lu off=%u len=%u remain=%u\r\n",
                                 (unsigned int)usb_tx_frame_id,
                                 (unsigned long)usb_tx_row,
                                 (unsigned int)usb_tx_row_offset,
                                 (unsigned int)tx_len,
                                 (unsigned int)(LINE_BYTES - (usb_tx_row_offset + tx_len)));
                    usb_row_debug_chunk_count++;
                }
                if (tx_len < VIDEO_PACKET_PAYLOAD_BYTES)
                {
                    usb_tx_short_packet_count++;
                }

                usb_tx_row_offset = (uint16_t)(usb_tx_row_offset + tx_len);
                if (usb_tx_row_offset >= LINE_BYTES)
                {
                    usb_tx_row_offset = 0;
                    usb_tx_row++;
                    usb_rows_sent = usb_tx_row;
                }

                /* Resume DVP capture */
                if ((frame_capture_done == 0U) && ((addr_cnt - usb_rows_sent) < DVP_LINE_BUFFER_COUNT) &&
                    ((DVP->CR0 & RB_DVP_ENABLE) == 0U) && (addr_cnt < IMG_HEIGHT))
                {
                    DVP->CR0 |= RB_DVP_ENABLE;
                }
            }
            else if ((frame_capture_done != 0U) && (usb_tx_row >= IMG_HEIGHT) && (usb_tx_row_offset == 0U))
            {
                usb_tx_frame_complete_count++;
                UART6_Printf("[usb] frame tx done fid=%u frames=%lu pkts=%lu short=%lu rows=%lu done=%lu ov=%lu\r\n",
                             (unsigned int)usb_tx_frame_id,
                             (unsigned long)usb_tx_frame_complete_count,
                             (unsigned long)usb_tx_packet_count,
                             (unsigned long)usb_tx_short_packet_count,
                             (unsigned long)usb_rows_sent,
                             (unsigned long)dvp_frame_done_cnt,
                             (unsigned long)dvp_fifo_overflow_cnt);
                usb_row_debug_enabled = 0;
                start_streaming_session((uint8_t)(usb_tx_frame_id + 1U));
            }
        }
    }
}
