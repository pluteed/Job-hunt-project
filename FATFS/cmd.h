//两边共用一套proto.c所以cmd需要和proto解耦，它负责下位机的命令分发
#ifndef __CMD_H
#define __CMD_H
#include <stdint.h>
#include "proto.h"


//注册：把CMD_Dispatch交给uart_io，收满一帧时由uart_io回调它
//必须在UART_IO_Init之后、进主循环之前调用一次
//漏掉这一句不会报错，但收满帧后没人分发，表现就是"收到数据但没反应"
void CMD_Init(void);

//由uart_io在成帧后调用，解析出来的一帧在这里分发
void CMD_Dispatch(const proto_frame_t *f);

//组一帧CMD_STATUS发出去，周期上报和STATUS_REQ都用它
void CMD_SendStatus(uint8_t seq);

#endif
