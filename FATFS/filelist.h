#ifndef __FILELIST_H
#define __FILELIST_H
#include <stdint.h>

uint8_t FILELIST_Mount(void);
uint16_t FILELIST_Count(void);
uint16_t FILELIST_GetTotal(void);
uint8_t FILELIST_NameByIndex(uint16_t idx,char* out,uint16_t *out_len);
/*列表项状态机，按照proto格式连发文件信息给上位机*/
/*中断中千万别碰Fatfs接口，不然会串口卡死*/
/*发送列表项不允许丢帧，所以走主循环，每次循环轮寻发送，缓冲区满就等待下一轮*/
uint8_t FILELIST_StartSend(uint8_t seq);
void FILELIST_Service(void);
uint8_t FILELIST_IsSending(void);

#endif
