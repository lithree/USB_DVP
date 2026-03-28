/********************************** (C) COPYRIGHT *******************************
 * File Name          : ch32v30x_usbhs_device.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2023/11/20
 * Description        : This file provides all the USBHS firmware functions.
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#include "ch32v30x_usbhs_device.h"

/******************************************************************************/
/* Variable Definition */
/* test mode */
volatile uint8_t USBHS_Test_Flag;
__attribute__((aligned(4))) uint8_t IFTest_Buf[53] =
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
        0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
        0xFE,                                                       // 26
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 37
        0x7F, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD,                         // 44
        0xFC, 0x7E, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD, 0x7E              // 53
    };

/* Global */
const uint8_t *pUSBHS_Descr;

/* Setup Request */
volatile uint8_t USBHS_SetupReqCode;
volatile uint8_t USBHS_SetupReqType;
volatile uint16_t USBHS_SetupReqValue;
volatile uint16_t USBHS_SetupReqIndex;
volatile uint16_t USBHS_SetupReqLen;

/* USB Device Status */
volatile uint8_t USBHS_DevConfig;
volatile uint8_t USBHS_DevAddr;
volatile uint16_t USBHS_DevMaxPackLen;
volatile uint8_t USBHS_DevSpeed;
volatile uint8_t USBHS_DevSleepStatus;
volatile uint8_t USBHS_DevEnumStatus;

/* Endpoint Buffer */
__attribute__((aligned(4))) uint8_t USBHS_EP0_Buf[DEF_USBD_UEP0_SIZE];
__attribute__((aligned(4))) uint8_t USBHS_EP3_Rx_Buf[DEF_USB_EP3_HS_SIZE];
__attribute__((aligned(4))) uint8_t USBHS_EP5_Rx_Buf[DEF_USB_EP5_HS_SIZE];
__attribute__((aligned(4))) uint8_t USBHS_EP4_Tx_Buf[DEF_USB_EP4_HS_SIZE];
__attribute__((aligned(4))) uint8_t USBHS_EP6_Tx_Buf[DEF_USB_EP6_HS_SIZE];

volatile uint16_t USBHS_EP3_Rx_Len = 0;

/* USB debug counters */
volatile uint32_t USBHS_EP3_Out_Cnt = 0;
volatile uint16_t USBHS_EP3_Last_Len = 0;
volatile uint32_t USBHS_EP4_In_Cnt = 0;

/* Mode switch flag from control transfer */
volatile uint8_t USBHS_Mode_Switch_Flag = 0;
volatile uint8_t USBHS_Mode_Switch_Value = 0;

/* Endpoint tx busy flag */
volatile uint8_t USBHS_Endp_Busy[DEF_UEP_NUM];

/* Ring buffer */
RING_BUFF_COMM RingBuffer_Comm;
__attribute__((aligned(4))) uint8_t Data_Buffer[DEF_RING_BUFFER_SIZE];

/******************************************************************************/
/* Interrupt Service Routine Declaration*/
void USBHS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      USB_TestMode_Deal
 *
 * @brief   Eye Diagram Test Function Processing.
 *
 * @return  none
 *
 */
void USB_TestMode_Deal(void)
{
    /* start test */
    USBHS_Test_Flag &= ~0x80;
    if (USBHS_SetupReqIndex == 0x0100)
    {
        /* Test_J */
        USBHSD->SUSPEND &= ~TEST_MASK;
        USBHSD->SUSPEND |= TEST_J;
    }
    else if (USBHS_SetupReqIndex == 0x0200)
    {
        /* Test_K */
        USBHSD->SUSPEND &= ~TEST_MASK;
        USBHSD->SUSPEND |= TEST_K;
    }
    else if (USBHS_SetupReqIndex == 0x0300)
    {
        /* Test_SE0_NAK */
        USBHSD->SUSPEND &= ~TEST_MASK;
        USBHSD->SUSPEND |= TEST_SE0;
    }
    else if (USBHS_SetupReqIndex == 0x0400)
    {
        /* Test_Packet */
        USBHSD->SUSPEND &= ~TEST_MASK;
        USBHSD->SUSPEND |= TEST_PACKET;

        USBHSD->CONTROL |= USBHS_UC_HOST_MODE;
        USBHSH->HOST_EP_CONFIG = USBHS_UH_EP_TX_EN | USBHS_UH_EP_RX_EN;
        USBHSH->HOST_EP_TYPE |= 0xff;

        USBHSH->HOST_TX_DMA = (uint32_t)(&IFTest_Buf[0]);
        USBHSH->HOST_TX_LEN = 53;
        USBHSH->HOST_EP_PID = (USB_PID_SETUP << 4);
        USBHSH->INT_FG = USBHS_UIF_TRANSFER;
    }
}

/*********************************************************************
 * @fn      USBHS_RCC_Init
 *
 * @brief   Initializes the clock for USB2.0 High speed device.
 *
 * @return  none
 */
void USBHS_RCC_Init(void)
{
    RCC_USBCLK48MConfig(RCC_USBCLK48MCLKSource_USBPHY);
    RCC_USBHSPLLCLKConfig(RCC_HSBHSPLLCLKSource_HSE);
    RCC_USBHSConfig(RCC_USBPLL_Div2);
    RCC_USBHSPLLCKREFCLKConfig(RCC_USBHSPLLCKREFCLK_4M);
    RCC_USBHSPHYPLLALIVEcmd(ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBHS, ENABLE);
}

/*********************************************************************
 * @fn      USBHS_Device_Endp_Init
 *
 * @brief   Initializes USB device endpoints.
 *
 * @return  none
 */
void USBHS_Device_Endp_Init(void)
{

    USBHSD->ENDP_CONFIG = USBHS_UEP1_T_EN | USBHS_UEP4_T_EN | USBHS_UEP6_T_EN |
                          USBHS_UEP1_R_EN | USBHS_UEP3_R_EN | USBHS_UEP5_R_EN;

    USBHSD->UEP0_MAX_LEN = DEF_USBD_UEP0_SIZE;
    USBHSD->UEP1_MAX_LEN = DEF_USB_EP1_HS_SIZE;
    USBHSD->UEP3_MAX_LEN = DEF_USB_EP3_HS_SIZE;
    USBHSD->UEP4_MAX_LEN = DEF_USB_EP4_HS_SIZE;
    USBHSD->UEP5_MAX_LEN = DEF_USB_EP5_HS_SIZE;
    USBHSD->UEP6_MAX_LEN = DEF_USB_EP6_HS_SIZE;

    USBHSD->UEP0_DMA = (uint32_t)(uint8_t *)USBHS_EP0_Buf;

    USBHSD->UEP1_RX_DMA = (uint32_t)(uint8_t *)Data_Buffer;
    USBHSD->UEP3_RX_DMA = (uint32_t)(uint8_t *)USBHS_EP3_Rx_Buf;
    USBHSD->UEP5_RX_DMA = (uint32_t)(uint8_t *)USBHS_EP5_Rx_Buf;

    USBHSD->UEP1_TX_DMA = (uint32_t)(uint8_t *)Data_Buffer;
    USBHSD->UEP4_TX_DMA = (uint32_t)(uint8_t *)USBHS_EP4_Tx_Buf;
    USBHSD->UEP6_TX_DMA = (uint32_t)(uint8_t *)USBHS_EP6_Tx_Buf;

    USBHSD->UEP0_TX_LEN = 0;
    USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_RES_NAK;
    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_RES_ACK;

    USBHSD->UEP1_RX_CTRL = USBHS_UEP_R_RES_ACK;
    USBHSD->UEP3_RX_CTRL = USBHS_UEP_R_RES_ACK;
    USBHSD->UEP5_RX_CTRL = USBHS_UEP_R_RES_ACK;

    USBHSD->UEP1_TX_LEN = 0;
    USBHSD->UEP1_TX_CTRL = USBHS_UEP_T_RES_NAK;

    USBHSD->UEP4_TX_LEN = 0;
    USBHSD->UEP4_TX_CTRL = USBHS_UEP_T_RES_NAK;

    USBHSD->UEP6_TX_LEN = 0;
    USBHSD->UEP6_TX_CTRL = USBHS_UEP_T_RES_NAK;

    /* Clear End-points Busy Status */
    for (uint8_t i = 0; i < DEF_UEP_NUM; i++)
    {
        USBHS_Endp_Busy[i] = 0;
    }
}

/*********************************************************************
 * @fn      USBHS_Device_Init
 *
 * @brief   Initializes USB high-speed device.
 *
 * @return  none
 */
void USBHS_Device_Init(FunctionalState sta)
{
    if (sta)
    {
        USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
        Delay_Us(10);
        USBHSD->CONTROL &= ~USBHS_UC_RESET_SIE;
        USBHSD->HOST_CTRL = USBHS_UH_PHY_SUSPENDM;
        USBHSD->CONTROL = USBHS_UC_DMA_EN | USBHS_UC_INT_BUSY | USBHS_UC_SPEED_HIGH;
        USBHSD->INT_EN = USBHS_UIE_SETUP_ACT | USBHS_UIE_TRANSFER | USBHS_UIE_DETECT | USBHS_UIE_BUS_RST | USBHS_UIE_SUSPEND;
        USBHS_Device_Endp_Init();
        USBHSD->CONTROL |= USBHS_UC_DEV_PU_EN;
        NVIC_EnableIRQ(USBHS_IRQn);
    }
    else
    {
        USBHSD->CONTROL = USBHS_UC_CLR_ALL | USBHS_UC_RESET_SIE;
        Delay_Us(10);
        USBHSD->CONTROL &= ~USBHS_UC_RESET_SIE;
        NVIC_DisableIRQ(USBHS_IRQn);
    }
}

/*********************************************************************
 * @fn      USBHS_Endp_DataUp
 *
 * @brief   usbhs device data upload
 * input: endp  - end-point numbers
 * *pubf - data buffer
 * len   - load data length
 * mod   - 0: DEF_UEP_DMA_LOAD 1: DEF_UEP_CPY_LOAD
 *
 * @return  none
 */
uint8_t USBHS_Endp_DataUp(uint8_t endp, uint8_t *pbuf, uint16_t len, uint8_t mod)
{
    uint8_t endp_buf_mode, endp_en, endp_tx_ctrl;

    /* DMA config, endp_ctrl config, endp_len config */
    if ((endp >= DEF_UEP1) && (endp <= DEF_UEP15))
    {
        endp_en = USBHSD->ENDP_CONFIG;
        if (endp_en & USBHSD_UEP_TX_EN(endp))
        {
            if ((USBHS_Endp_Busy[endp] & DEF_UEP_BUSY) == 0x00)
            {
                endp_buf_mode = USBHSD->BUF_MODE;
                /* if end-point buffer mode is double buffer */
                if (endp_buf_mode & USBHSD_UEP_DOUBLE_BUF(endp))
                {
                    /* end-point buffer mode is double buffer */
                    /* only end-point tx enable  */
                    if ((endp_en & USBHSD_UEP_RX_EN(endp)) == 0x00)
                    {
                        endp_tx_ctrl = USBHSD_UEP_TXCTRL(endp);
                        if (mod == DEF_UEP_DMA_LOAD)
                        {
                            if ((endp_tx_ctrl & USBHS_UEP_T_TOG_DATA1) == 0)
                            {
                                /* use UEPn_TX_DMA */
                                USBHSD_UEP_TXDMA(endp) = (uint32_t)pbuf;
                            }
                            else
                            {
                                /* use UEPn_RX_DMA */
                                USBHSD_UEP_RXDMA(endp) = (uint32_t)pbuf;
                            }
                        }
                        else if (mod == DEF_UEP_CPY_LOAD)
                        {
                            if ((endp_tx_ctrl & USBHS_UEP_T_TOG_DATA1) == 0)
                            {
                                /* use UEPn_TX_DMA */
                                memcpy(USBHSD_UEP_TXBUF(endp), pbuf, len);
                            }
                            else
                            {
                                /* use UEPn_RX_DMA */
                                memcpy(USBHSD_UEP_RXBUF(endp), pbuf, len);
                            }
                        }
                        else
                        {
                            return 1;
                        }
                    }
                    else
                    {
                        return 1;
                    }
                }
                else
                {
                    /* end-point buffer mode is single buffer */
                    if (mod == DEF_UEP_DMA_LOAD)
                    {

                        USBHSD_UEP_TXDMA(endp) = (uint32_t)pbuf;
                    }
                    else if (mod == DEF_UEP_CPY_LOAD)
                    {
                        memcpy(USBHSD_UEP_TXBUF(endp), pbuf, len);
                    }
                    else
                    {
                        return 1;
                    }
                }
                /* Set end-point busy */
                USBHS_Endp_Busy[endp] |= DEF_UEP_BUSY;
                /* end-point n response tx ack */
                USBHSD_UEP_TLEN(endp) = len;
                USBHSD_UEP_TXCTRL(endp) = (USBHSD_UEP_TXCTRL(endp) &= ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
            }
            else
            {
                return 1;
            }
        }
        else
        {
            return 1;
        }
    }
    else
    {
        return 1;
    }

    return 0;
}

/*********************************************************************
 * @fn      USBHS_IRQHandler
 *
 * @brief   This function handles USBHS exception.
 *
 * @return  none
 */
void USBHS_IRQHandler(void)
{
    uint8_t intflag, intst, errflag;
    uint16_t len, i;

    intflag = USBHSD->INT_FG;
    intst = USBHSD->INT_ST;

    if (intflag & USBHS_UIF_TRANSFER)
    {
        switch (intst & USBHS_UIS_TOKEN_MASK)
        {
        /* data-in stage processing */
        case USBHS_UIS_TOKEN_IN:
            switch (intst & (USBHS_UIS_TOKEN_MASK | USBHS_UIS_ENDP_MASK))
            {
            /* end-point 0 data in interrupt */
            case USBHS_UIS_TOKEN_IN | DEF_UEP0:
                if (USBHS_SetupReqLen == 0)
                {
                    USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_ACK;
                }
                if ((USBHS_SetupReqType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD)
                {
                    /* Non-standard request endpoint 0 Data upload */
                }
                else
                {
                    /* Standard request endpoint 0 Data upload */
                    switch (USBHS_SetupReqCode)
                    {
                    case USB_GET_DESCRIPTOR:
                        len = USBHS_SetupReqLen >= DEF_USBD_UEP0_SIZE ? DEF_USBD_UEP0_SIZE : USBHS_SetupReqLen;
                        memcpy(USBHS_EP0_Buf, pUSBHS_Descr, len);
                        USBHS_SetupReqLen -= len;
                        pUSBHS_Descr += len;
                        USBHSD->UEP0_TX_LEN = len;
                        USBHSD->UEP0_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
                        break;

                    case USB_SET_ADDRESS:
                        USBHSD->DEV_AD = USBHS_DevAddr;
                        break;

                    default:
                        USBHSD->UEP0_TX_LEN = 0;
                        break;
                    }
                }

                /* test mode */
                if (USBHS_Test_Flag & 0x80)
                {
                    USB_TestMode_Deal();
                }
                break;

            /* end-point 1 data in interrupt */
            case USBHS_UIS_TOKEN_IN | DEF_UEP1:
                USBHSD->UEP1_TX_CTRL = (USBHSD->UEP1_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
                USBHSD->UEP1_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
                USBHS_Endp_Busy[DEF_UEP1] &= ~DEF_UEP_BUSY;
                break;

            /* --- FINAL CORRECTED VERSION: end-point 4 data in interrupt --- */
            case USBHS_UIS_TOKEN_IN | DEF_UEP4:
                USBHSD->UEP4_TX_CTRL = (USBHSD->UEP4_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
                USBHSD->UEP4_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1; /* <-- THIS LINE FIXES THE TIMEOUT BUG */
                USBHS_Endp_Busy[DEF_UEP4] &= ~DEF_UEP_BUSY;
                USBHS_EP4_In_Cnt++;
                break;

            /* end-point 6 data in interrupt */
            case USBHS_UIS_TOKEN_IN | DEF_UEP6:
                USBHSD->UEP6_TX_CTRL = (USBHSD->UEP6_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_NAK;
                USBHSD->UEP6_TX_CTRL ^= USBHS_UEP_T_TOG_DATA1;
                USBHS_Endp_Busy[DEF_UEP6] &= ~DEF_UEP_BUSY;
                USBHSD->UEP5_RX_CTRL = ((USBHSD->UEP5_RX_CTRL) & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_ACK;
                break;

            default:
                break;
            }
            break;

        /* data-out stage processing */
        case USBHS_UIS_TOKEN_OUT:
            switch (intst & (USBHS_UIS_TOKEN_MASK | USBHS_UIS_ENDP_MASK))
            {

            /* end-point 0 data out interrupt */
            case USBHS_UIS_TOKEN_OUT | DEF_UEP0:
                len = USBHSD->RX_LEN;
                if (intst & USBHS_UIS_TOG_OK)
                {
                    if ((USBHS_SetupReqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_VENDOR)
                    {
                        /* Handle vendor request data stage */
                        if (USBHS_SetupReqCode == 0x02)
                        {
                            /* Mode Switch Command - data received in EP0_Buf */
                            /* No additional action needed; mode is in the request Value field */
                        }
                        else if (USBHS_SetupReqCode == 0x01)
                         {
                             LED_status = USBHS_EP0_Buf[0];
                             LED_flag = 1;
                         }
                    }
                    USBHS_SetupReqLen -= len;
                    if (USBHS_SetupReqLen == 0)
                    {
                        USBHSD->UEP0_TX_LEN = 0;
                        USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
                    }
                }
                break;

            /* end-point 1 data out interrupt */
            case USBHS_UIS_TOKEN_OUT | DEF_UEP1:
                if (intst & USBHS_UIS_TOG_OK)
                {
                    USBHSD->UEP1_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
                    RingBuffer_Comm.PackLen[RingBuffer_Comm.LoadPtr] = USBHSD->RX_LEN;
                    RingBuffer_Comm.LoadPtr++;
                    if (RingBuffer_Comm.LoadPtr == DEF_Ring_Buffer_Max_Blks)
                        RingBuffer_Comm.LoadPtr = 0;
                    USBHSD->UEP1_RX_DMA = (uint32_t)(&Data_Buffer[(RingBuffer_Comm.LoadPtr) * DEF_USBD_HS_PACK_SIZE]);
                    RingBuffer_Comm.RemainPack++;
                    if (RingBuffer_Comm.RemainPack >= DEF_Ring_Buffer_Max_Blks - DEF_RING_BUFFER_REMINE)
                    {
                        USBHSD->UEP1_RX_CTRL = ((USBHSD->UEP1_RX_CTRL) & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
                        RingBuffer_Comm.StopFlag = 1;
                    }
                }
                break;

            /* end-point 3 data out interrupt */
            case USBHS_UIS_TOKEN_OUT | DEF_UEP3:
                if (intst & USBHS_UIS_TOG_OK)
                {
                    USBHS_EP3_Rx_Len = USBHSD->RX_LEN;
                    USBHS_EP3_Last_Len = USBHSD->RX_LEN;
                    USBHS_EP3_Out_Cnt++;
                    USBHSD->UEP3_RX_CTRL = (USBHSD->UEP3_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
                    USBHSD->UEP3_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
                }
                break;

            /* end-point 5 data out interrupt */
            case USBHS_UIS_TOKEN_OUT | DEF_UEP5:
                if (intst & USBHS_UIS_TOG_OK)
                {
                    len = (uint16_t)(USBHSD->RX_LEN);
                    USBHSD->UEP5_RX_CTRL ^= USBHS_UEP_R_TOG_DATA1;
                    USBHSD->UEP5_RX_CTRL = ((USBHSD->UEP5_RX_CTRL) & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_NAK;
                    for (i = 0; i < len; i++)
                    {
                        USBHS_EP6_Tx_Buf[i] = ~USBHS_EP5_Rx_Buf[i];
                    }
                    USBHSD->UEP6_TX_LEN = len;
                    USBHSD->UEP6_TX_CTRL = (USBHSD->UEP6_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_ACK;
                }
                break;

            default:
                errflag = 0xFF;
                break;
            }
            break;

        case USBHS_UIS_TOKEN_SOF:
            break;

        default:
            break;
        }
        USBHSD->INT_FG = USBHS_UIF_TRANSFER;
    }
    else if (intflag & USBHS_UIF_SETUP_ACT)
    {
        USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_NAK;
        USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_NAK;

        /* Store All Setup Values */
        USBHS_SetupReqType = pUSBHS_SetupReqPak->bRequestType;
        USBHS_SetupReqCode = pUSBHS_SetupReqPak->bRequest;
        USBHS_SetupReqLen = pUSBHS_SetupReqPak->wLength;
        USBHS_SetupReqValue = pUSBHS_SetupReqPak->wValue;
        USBHS_SetupReqIndex = pUSBHS_SetupReqPak->wIndex;

        len = 0;
        errflag = 0;

        if ((USBHS_SetupReqType & USB_REQ_TYP_MASK) == USB_REQ_TYP_VENDOR)
        {
            /* Correctly handle the vendor request based on the host application */
            if( USBHS_SetupReqCode == 0x01 )
            {
                LED_status = (USBHS_SetupReqValue > 0) ? 1 : 0;
                LED_flag = 1;
                len = 0; // Prepare for the zero-length status stage
            }
            else if (USBHS_SetupReqCode == 0x02)
            {
                /* Mode Switch Command - Use Control Transfer on EP0 */
                /* Request Value contains the display mode */
                USBHS_Mode_Switch_Value = (uint8_t)(USBHS_SetupReqValue & 0xFF);
                USBHS_Mode_Switch_Flag = 1;
                len = 0;
            }
            else
            {
                errflag = 0xFF;
            }
        }
        else
        {
            switch (USBHS_SetupReqCode)
            {
            case USB_GET_DESCRIPTOR:
                switch ((uint8_t)(USBHS_SetupReqValue >> 8))
                {
                case USB_DESCR_TYP_DEVICE:
                    pUSBHS_Descr = MyDevDescr;
                    len = DEF_USBD_DEVICE_DESC_LEN;
                    break;

                case USB_DESCR_TYP_CONFIG:
                    if ((USBHSD->SPEED_TYPE & USBHS_SPEED_TYPE_MASK) == USBHS_SPEED_HIGH)
                    {
                        USBHS_DevSpeed = USBHS_SPEED_HIGH;
                        USBHS_DevMaxPackLen = DEF_USBD_HS_PACK_SIZE;
                    }
                    else
                    {
                        USBHS_DevSpeed = USBHS_SPEED_FULL;
                        USBHS_DevMaxPackLen = DEF_USBD_FS_PACK_SIZE;
                    }

                    if (USBHS_DevSpeed == USBHS_SPEED_HIGH)
                    {
                        pUSBHS_Descr = MyCfgDescr_HS;
                        len = DEF_USBD_CONFIG_HS_DESC_LEN;
                    }
                    else
                    {
                        pUSBHS_Descr = MyCfgDescr_FS;
                        len = DEF_USBD_CONFIG_FS_DESC_LEN;
                    }
                    break;

                case USB_DESCR_TYP_STRING:
                    switch ((uint8_t)(USBHS_SetupReqValue & 0xFF))
                    {
                    case DEF_STRING_DESC_LANG:
                        pUSBHS_Descr = MyLangDescr;
                        len = DEF_USBD_LANG_DESC_LEN;
                        break;
                    case DEF_STRING_DESC_MANU:
                        pUSBHS_Descr = MyManuInfo;
                        len = DEF_USBD_MANU_DESC_LEN;
                        break;
                    case DEF_STRING_DESC_PROD:
                        pUSBHS_Descr = MyProdInfo;
                        len = DEF_USBD_PROD_DESC_LEN;
                        break;
                    case DEF_STRING_DESC_SERN:
                        pUSBHS_Descr = MySerNumInfo;
                        len = DEF_USBD_SN_DESC_LEN;
                        break;
                    default:
                        errflag = 0xFF;
                        break;
                    }
                    break;

                case USB_DESCR_TYP_QUALIF:
                    pUSBHS_Descr = MyQuaDesc;
                    len = DEF_USBD_QUALFY_DESC_LEN;
                    break;

                case USB_DESCR_TYP_SPEED:
                    if (USBHS_DevSpeed == USBHS_SPEED_HIGH)
                    {
                        memcpy(&TAB_USB_HS_OSC_DESC[2], &MyCfgDescr_FS[2], DEF_USBD_CONFIG_FS_DESC_LEN - 2);
                        pUSBHS_Descr = (uint8_t *)&TAB_USB_HS_OSC_DESC[0];
                        len = DEF_USBD_CONFIG_FS_DESC_LEN;
                    }
                    else if (USBHS_DevSpeed == USBHS_SPEED_FULL)
                    {
                        memcpy(&TAB_USB_FS_OSC_DESC[2], &MyCfgDescr_HS[2], DEF_USBD_CONFIG_HS_DESC_LEN - 2);
                        pUSBHS_Descr = (uint8_t *)&TAB_USB_FS_OSC_DESC[0];
                        len = DEF_USBD_CONFIG_HS_DESC_LEN;
                    }
                    else
                    {
                        errflag = 0xFF;
                    }
                    break;

                default:
                    errflag = 0xFF;
                    break;
                }

                if (USBHS_SetupReqLen > len)
                    USBHS_SetupReqLen = len;
                len = (USBHS_SetupReqLen >= DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBHS_SetupReqLen;
                memcpy(USBHS_EP0_Buf, pUSBHS_Descr, len);
                pUSBHS_Descr += len;
                break;

            case USB_SET_ADDRESS:
                USBHS_DevAddr = (uint16_t)(USBHS_SetupReqValue & 0xFF);
                break;

            case USB_GET_CONFIGURATION:
                USBHS_EP0_Buf[0] = USBHS_DevConfig;
                if (USBHS_SetupReqLen > 1)
                    USBHS_SetupReqLen = 1;
                break;

            case USB_SET_CONFIGURATION:
                USBHS_DevConfig = (uint8_t)(USBHS_SetupReqValue & 0xFF);
                USBHS_DevEnumStatus = 0x01;
                /* CORRECTED: Removed automatic priming of EP4 */
                break;

            case USB_CLEAR_FEATURE:
                if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                {
                    if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == 0x01)
                        USBHS_DevSleepStatus &= ~0x01;
                    else
                        errflag = 0xFF;
                }
                else if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                {
                    switch ((uint8_t)(USBHS_SetupReqIndex & 0xFF))
                    {
                    case (DEF_UEP1 | DEF_UEP_OUT):
                        USBHSD->UEP1_RX_CTRL = USBHS_UEP_R_RES_ACK;
                        break;
                    case (DEF_UEP1 | DEF_UEP_IN):
                        USBHSD->UEP1_TX_CTRL = USBHS_UEP_T_RES_NAK;
                        break;
                    case (DEF_UEP3 | DEF_UEP_OUT):
                        USBHSD->UEP3_RX_CTRL = USBHS_UEP_R_RES_ACK;
                        break;
                    case (DEF_UEP4 | DEF_UEP_IN):
                        USBHSD->UEP4_TX_CTRL = USBHS_UEP_T_RES_NAK;
                        break;
                    case (DEF_UEP5 | DEF_UEP_OUT):
                        USBHSD->UEP5_RX_CTRL = USBHS_UEP_R_RES_ACK;
                        break;
                    case (DEF_UEP6 | DEF_UEP_IN):
                        USBHSD->UEP6_TX_CTRL = USBHS_UEP_T_RES_NAK;
                        break;
                    default:
                        errflag = 0xFF;
                        break;
                    }
                }
                else
                    errflag = 0xFF;
                break;

            case USB_SET_FEATURE:
                if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                {
                    if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == USB_REQ_FEAT_REMOTE_WAKEUP)
                    {
                        if (MyCfgDescr_FS[7] & 0x20)
                            USBHS_DevSleepStatus |= 0x01;
                        else
                            errflag = 0xFF;
                    }
                    else if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == 0x02)
                    {
                        if ((USBHS_SetupReqIndex == 0x0100) || (USBHS_SetupReqIndex == 0x0200) ||
                            (USBHS_SetupReqIndex == 0x0300) || (USBHS_SetupReqIndex == 0x0400))
                            USBHS_Test_Flag |= 0x80;
                    }
                    else
                        errflag = 0xFF;
                }
                else if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                {
                    if ((uint8_t)(USBHS_SetupReqValue & 0xFF) == USB_REQ_FEAT_ENDP_HALT)
                    {
                        switch ((uint8_t)(USBHS_SetupReqIndex & 0xFF))
                        {
                        case (DEF_UEP1 | DEF_UEP_OUT):
                            USBHSD->UEP1_RX_CTRL = (USBHSD->UEP1_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_STALL;
                            break;
                        case (DEF_UEP1 | DEF_UEP_IN):
                            USBHSD->UEP1_TX_CTRL = (USBHSD->UEP1_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL;
                            break;
                        case (DEF_UEP3 | DEF_UEP_OUT):
                            USBHSD->UEP3_RX_CTRL = (USBHSD->UEP3_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_STALL;
                            break;
                        case (DEF_UEP4 | DEF_UEP_IN):
                            USBHSD->UEP4_TX_CTRL = (USBHSD->UEP4_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL;
                            break;
                        case (DEF_UEP5 | DEF_UEP_OUT):
                            USBHSD->UEP5_RX_CTRL = (USBHSD->UEP5_RX_CTRL & ~USBHS_UEP_R_RES_MASK) | USBHS_UEP_R_RES_STALL;
                            break;
                        case (DEF_UEP6 | DEF_UEP_IN):
                            USBHSD->UEP6_TX_CTRL = (USBHSD->UEP6_TX_CTRL & ~USBHS_UEP_T_RES_MASK) | USBHS_UEP_T_RES_STALL;
                            break;
                        default:
                            errflag = 0xFF;
                            break;
                        }
                    }
                }
                else
                    errflag = 0xFF;
                break;

            case USB_GET_INTERFACE:
                USBHS_EP0_Buf[0] = 0x00;
                if (USBHS_SetupReqLen > 1)
                    USBHS_SetupReqLen = 1;
                break;

            case USB_GET_STATUS:
                USBHS_EP0_Buf[0] = 0x00;
                USBHS_EP0_Buf[1] = 0x00;
                if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP)
                {
                    switch ((uint8_t)(USBHS_SetupReqIndex & 0xFF))
                    {
                    case (DEF_UEP_OUT | DEF_UEP1):
                        if (((USBHSD->UEP1_RX_CTRL) & USBHS_UEP_R_RES_MASK) == USBHS_UEP_R_RES_STALL)
                            USBHS_EP0_Buf[0] = 0x01;
                        break;
                    case (DEF_UEP_IN | DEF_UEP1):
                        if (((USBHSD->UEP1_TX_CTRL) & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL)
                            USBHS_EP0_Buf[0] = 0x01;
                        break;
                    case (DEF_UEP_OUT | DEF_UEP3):
                        if (((USBHSD->UEP3_RX_CTRL) & USBHS_UEP_R_RES_MASK) == USBHS_UEP_R_RES_STALL)
                            USBHS_EP0_Buf[0] = 0x01;
                        break;
                    case (DEF_UEP_IN | DEF_UEP4):
                        if (((USBHSD->UEP4_TX_CTRL) & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL)
                            USBHS_EP0_Buf[0] = 0x01;
                        break;
                    case (DEF_UEP_OUT | DEF_UEP5):
                        if (((USBHSD->UEP5_RX_CTRL) & USBHS_UEP_R_RES_MASK) == USBHS_UEP_R_RES_STALL)
                            USBHS_EP0_Buf[0] = 0x01;
                        break;
                    case (DEF_UEP_IN | DEF_UEP6):
                        if (((USBHSD->UEP6_TX_CTRL) & USBHS_UEP_T_RES_MASK) == USBHS_UEP_T_RES_STALL)
                            USBHS_EP0_Buf[0] = 0x01;
                        break;
                    default:
                        errflag = 0xFF;
                        break;
                    }
                }
                else if ((USBHS_SetupReqType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_DEVICE)
                {
                    if (USBHS_DevSleepStatus & 0x01)
                        USBHS_EP0_Buf[0] = 0x02;
                }
                if (USBHS_SetupReqLen > 2)
                    USBHS_SetupReqLen = 2;
                break;

            default:
                errflag = 0xFF;
                break;
            }
        }

        if (errflag == 0xFF)
        {
            USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_STALL;
            USBHSD->UEP0_RX_CTRL = USBHS_UEP_R_TOG_DATA1 | USBHS_UEP_R_RES_STALL;
        }
        else
        {
            if (USBHS_SetupReqType & DEF_UEP_IN)
            {
                len = (USBHS_SetupReqLen > DEF_USBD_UEP0_SIZE) ? DEF_USBD_UEP0_SIZE : USBHS_SetupReqLen;
                USBHS_SetupReqLen -= len;
                USBHSD->UEP0_TX_LEN = len;
                USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
            }
            else
            {
                if(USBHS_SetupReqLen == 0)
                {
                     USBHSD->UEP0_TX_LEN = 0;
                     USBHSD->UEP0_TX_CTRL = USBHS_UEP_T_TOG_DATA1 | USBHS_UEP_T_RES_ACK;
                }
            }
        }
        USBHSD->INT_FG = USBHS_UIF_SETUP_ACT;
    }
    else if (intflag & USBHS_UIF_BUS_RST)
    {
        USBHS_DevConfig = 0;
        USBHS_DevAddr = 0;
        USBHS_DevSleepStatus = 0;
        USBHS_DevEnumStatus = 0;
        USBHSD->DEV_AD = 0;
        USBHS_Device_Endp_Init();
        USBHSD->INT_FG = USBHS_UIF_BUS_RST;
    }
    else if (intflag & USBHS_UIF_SUSPEND)
    {
        USBHSD->INT_FG = USBHS_UIF_SUSPEND;
        Delay_Us(10);
        if (USBHSD->MIS_ST & USBHS_UMS_SUSPEND)
        {
            USBHS_DevSleepStatus |= 0x02;
            if (USBHS_DevSleepStatus == 0x03)
            { /* Handling usb sleep here */
            }
        }
        else
        {
            USBHS_DevSleepStatus &= ~0x02;
        }
    }
    else
    {
        USBHSD->INT_FG = intflag;
    }
}
