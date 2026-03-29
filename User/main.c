#include "Headfile.h"
#include <stdio.h>
#include <stdarg.h>
#include "ov.h"

/* DVP Work Mode */
#define JPEG_MODE     1
#define RGB565_MODE   2

#define DVP_Work_Mode    RGB565_MODE

/* DVP Image buffer */
__attribute__((aligned(4))) uint8_t image_buffer[LINE_BYTES * 2];

volatile UINT32 frame_cnt = 0;
volatile UINT32 addr_cnt = 0;
volatile UINT32 href_cnt = 0;
volatile uint8_t frame_capture_done = 0;
volatile uint32_t dvp_fifo_overflow_cnt = 0;
volatile uint32_t dvp_frame_done_cnt = 0;
volatile uint32_t dvp_frame_stop_cnt = 0;
volatile uint32_t usb_rows_sent = 0;

void DVP_IRQHandler (void) __attribute__((interrupt("WCH-Interrupt-fast")));

void DVP_ResetCaptureState(void)
{
    DVP->DMA_BUF0 = (uint32_t)&image_buffer[0];
    DVP->DMA_BUF1 = (uint32_t)&image_buffer[LINE_BYTES];
    DVP->IFR = 0;
    addr_cnt = 0;
    href_cnt = 0;
    frame_capture_done = 0;
}

static void Report_Reset_Cause(void)
{
    RCC_ClearFlag();
}

void UART6_Init(uint32_t baudrate)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    USART_InitTypeDef USART_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART6, ENABLE);

    /* PC0 -> TX */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    /* PC1 -> RX */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(UART6, &USART_InitStructure);
    USART_Cmd(UART6, ENABLE);
}

void UART6_Send_Byte(u8 t)
{
    while (USART_GetFlagStatus(UART6, USART_FLAG_TC) == RESET);
    USART_SendData(UART6, t);
}

void UART6_Printf(char *format, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, format);
    vsnprintf(buf, sizeof(buf), format, ap);
    va_end(ap);

    char *p = buf;
    while (*p)
    {
        while (USART_GetFlagStatus(UART6, USART_FLAG_TXE) == RESET);
        USART_SendData(UART6, *p);
        p++;
    }
}

void GPIO_Toggle_INIT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void DVP_Init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure={0};

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DVP, ENABLE);

    DVP->CR0 &= ~RB_DVP_MSK_DAT_MOD;
    DVP->CR0 |= RB_DVP_D8_MOD | RB_DVP_V_POLAR | RB_DVP_P_POLAR;
    DVP->CR1 &= ~(RB_DVP_ALL_CLR| RB_DVP_RCV_CLR);

    DVP->COL_NUM = LINE_BYTES;

    DVP_ResetCaptureState();

    DVP->CR1 &= ~RB_DVP_FCRC;
    DVP->CR1 |= DVP_RATE_100P;

    DVP->IER |= RB_DVP_IE_STP_FRM;
    DVP->IER |= RB_DVP_IE_FIFO_OV;
    DVP->IER |= RB_DVP_IE_FRM_DONE;
    DVP->IER |= RB_DVP_IE_ROW_DONE;
    DVP->IER |= RB_DVP_IE_STR_FRM;

    NVIC_InitStructure.NVIC_IRQChannel = DVP_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    DVP->CR1 |= RB_DVP_DMA_EN;
    DVP->CR0 |= RB_DVP_ENABLE;
}

void DVP_IRQHandler(void)
{
    uint8_t mark_done = 0;

    if (DVP->IFR & RB_DVP_IF_ROW_DONE)
    {
        DVP->IFR &= ~RB_DVP_IF_ROW_DONE;

        href_cnt++;

        addr_cnt++;

        /* Transfer sufficient line, mark done */
        if ((frame_capture_done == 0U) && (addr_cnt >= IMG_HEIGHT))
        {
            mark_done = 1;
        }

        /* Pause capture if DVP buffers are full. */
        if ((frame_capture_done == 0U) && ((addr_cnt - usb_rows_sent) >= 2U))
        {
            DVP->CR0 &= ~RB_DVP_ENABLE;
        }
    }

    if (DVP->IFR & RB_DVP_IF_FRM_DONE)
    {
        DVP->IFR &= ~RB_DVP_IF_FRM_DONE;

        mark_done = 1;
    }

    if (DVP->IFR & RB_DVP_IF_STR_FRM)
    {
        DVP->IFR &= ~RB_DVP_IF_STR_FRM;

        frame_cnt++;
        addr_cnt = 0;
        href_cnt = 0;
        usb_rows_sent = 0;
    }

    if (DVP->IFR & RB_DVP_IF_STP_FRM)
    {
        DVP->IFR &= ~RB_DVP_IF_STP_FRM;

        // /* Stop frame, mark done */
        // if ((frame_capture_done == 0U) && (addr_cnt > 0U))
        // {
        //     mark_done = 1;
        //     dvp_frame_stop_cnt++;
        // }
    }

    if (DVP->IFR & RB_DVP_IF_FIFO_OV)
    {
        DVP->IFR &= ~RB_DVP_IF_FIFO_OV;
        dvp_fifo_overflow_cnt++;

        DVP->CR0 &= ~RB_DVP_ENABLE;
        DVP_ResetCaptureState();
        DVP->CR0 |= RB_DVP_ENABLE;
    }

    if ((mark_done != 0U) && (frame_capture_done == 0U))
    {
        DVP->CR0 &= ~RB_DVP_ENABLE;
        frame_capture_done = 1;
        dvp_frame_done_cnt++;
    }
}

int main(void)
{
    uint32_t last_frame_done_cnt = 0;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    
    UART6_Init(921600);
    Report_Reset_Cause();
    
    USBHS_RCC_Init();
    USBHS_Device_Init(ENABLE);
    NVIC_EnableIRQ(USBHS_IRQn);

    GPIO_Toggle_INIT();
    
    OLED_Init();
    OLED_Clear();
    OLED_ShowString(0, 0, "Waiting for USB...");
    OLED_Refresh();

    while(OV2640_Init())
    {
        Delay_Ms(1000);
    }

    Delay_Ms(1000);

    RGB565_Mode_Init();
    Delay_Ms(100);
    OV2640_OutSize_Set(IMG_WIDTH, IMG_HEIGHT);
    Delay_Ms(1000);

    DVP_Init();

    while (1)
    {
        USB_bulk_data_handler();

        if (dvp_frame_done_cnt != last_frame_done_cnt)
        {
            last_frame_done_cnt = dvp_frame_done_cnt;
        }
    }
}
