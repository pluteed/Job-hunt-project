//app.c 应用整合
//当前是「最小PING版」：只装配串口收发链路，用来验证
//   上位机发帧 -> USART1空闲中断 -> ring -> 状态机成帧 -> CMD_Dispatch -> 组PONG -> 发送中断
//SD卡和VS1053那几步先注释留位置，等filelist/player写完再逐行打开
#include "app.h"

#include "proto.h"
#include "uart_io.h"
#include "cmd.h"
#include "player.h"
#include "filelist.h"

void App_Init(void){
    //1.串口IO，内部会启动空闲中断接收，必须最先
    UART_IO_Init();

    //2.协议层：注册发送钩子 + 解析状态机归位 + 注册收帧回调
    PROTO_SetTxHook(UART_IO_SendHook);
    PROTO_ParserInit();
    CMD_Init();                 //内部就是 UART_IO_SetFrameHandler(CMD_Dispatch)

    //3.文件系统（等filelist写完再打开）
    if(FILELIST_Mount()==0){
       PLAYER_SetTotal(FILELIST_Count());
    }

    //4.VS1053（等player写完、VS_Init里没有printf依赖后再打开）
    //VS_Init();
    //VS_SetVolume(0x30,0x30);

    //5.TIM3时钟泵，必须最后启动
    //VS_PumpInit();
}

void App_Poll(void){
    //1.收字节 -> 解帧 -> 分发命令（内部会调CMD_Dispatch）
    UART_IO_Poll();

    //2.列表连发（等filelist写完再打开）
    FILELIST_Service();

    //3.播放器填缓冲（等player写完再打开）
    //PLAYER_Service();

    //4.每秒主动上报状态（等真实现后打开，间隔1000ms）
    //static uint32_t s_tick=0;
    //if(HAL_GetTick()-s_tick>=1000u){ s_tick=HAL_GetTick(); CMD_SendStatus(0); }
}
