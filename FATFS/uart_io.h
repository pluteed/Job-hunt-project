/* 内有两个环形缓冲栈，usart串口只是将数据地址抄走，而在之后的中断里才开始发送，
   所以需要将数据转入static堆区，而且这样做还可以让帧信息排队，防止连发的信息丢失 */


#ifndef __UART_IO_H
#define __UART_IO_H


#include "stm32f1xx_hal.h"
#include "proto.h"
#include <stdint.h>


#define UART_IO_TX_RING             512u
#define UART_IO_RX_RING             256u
#define UART_IO_RX_LINE             64u
#define UART_IO_MAX_FEED            64u


/* 初始化：清零缓冲、注册钩子、启动空闲中断接收 */
void     UART_IO_Init(void);

/* 主循环调用：①冲刷发送环形缓冲 ②把收到的字节喂给协议状态机 */
void     UART_IO_Poll(void);

/* 供 PROTO_SetTxHook() 注册的发送钩子：把一帧拷进环形缓冲
 *   返回  0 = 已接收；<0 = 缓冲不够，本帧没发（调用方稍后重试） */
int8_t   UART_IO_SendHook(const uint8_t *buf, uint16_t len);

/* 由 hal_hooks.c 的 HAL_UARTEx_RxEventCallback 转发进来 */
void     UART_IO_RxEvent(UART_HandleTypeDef *huart, uint16_t size);

/* 由 hal_hooks.c 的 HAL_UART_TxCpltCallback 转发进来 */
void     UART_IO_TxDone(UART_HandleTypeDef *huart);

/* 收到一整帧时的回调注册（app.c 注册成 CMD_Dispatch） */
typedef void (*uart_io_frame_cb_t)(const proto_frame_t *f);
void     UART_IO_SetFrameHandler(uart_io_frame_cb_t cb);

/* 诊断用 */
uint16_t UART_IO_RxPending(void);
uint16_t UART_IO_TxPending(void);



#endif
