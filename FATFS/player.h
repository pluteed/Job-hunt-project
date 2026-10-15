//player.h 播放业务层接口
//注意：当前player.c是最小桩，所有控制函数返回ACK_ERR_CMD表示"还没实现"
//真正实现时只改player.c，本头文件不用动
#ifndef __PLAYER_H
#define __PLAYER_H

#include <stdint.h>
#include "proto.h"


#define PLAYER_BUF_SIZE         512u    /*单块缓冲大小，本文件开辟两块*/
#define PLAYER_ENDFILL          2052u   /*VS模块要求，流结束需要灌2052字节*/
#define PLAYER_ENDFILL2         32u

#define PLAYER_MODE_MANUAL 0u     /* 手动：曲终停住，等上位机发下一首 */
#define PLAYER_MODE_ONE    1u     /* 单曲循环：曲终自动重开当前首 */
#define PLAYER_MODE_SEQ    2u  

void     PLAYER_Init(void);

/* 主循环每圈调用一次：填缓冲 → 起泵 → 收尾 → 更新进度 */
void     PLAYER_Service(void);

//控制接口，返回值就是ACK_*结果码，直接塞进SendAck
uint8_t  PLAYER_Play(uint16_t idx);
uint8_t  PLAYER_Pause(void);
uint8_t  PLAYER_Resume(void);
uint8_t  PLAYER_Stop(void);
uint8_t  PLAYER_Next(void);
uint8_t  PLAYER_Prev(void);
uint8_t  PLAYER_SetVol(uint8_t vol);   //vol=0~100，本层负责映射到VS1053的SCI_VOL
uint8_t  PLAYER_SetMode(uint8_t mode); //mode=PLAYER_MODE_*，越界回 ACK_ERR_PAR

//状态查询，供CMD_SendStatus组帧
uint8_t  PLAYER_GetState(void);
uint16_t PLAYER_GetIndex(void);
uint32_t PLAYER_GetPosSec(void);
uint32_t PLAYER_GetDurSec(void);
uint8_t  PLAYER_GetVol(void);          //0~100，与SetVol同一标度

/* 曲目总数由 filelist 告知，用于上下首绕回 */
void     PLAYER_SetTotal(uint16_t total);

#endif
