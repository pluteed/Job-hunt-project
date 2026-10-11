# STM32 双机音乐播放器

下位机用 F103C8T6，负责从 SD 卡读文件、送进 VS1053 硬解码出声；上位机用 F407，跑 LVGL 界面和
FreeRTOS。两块板之间用 USART 通信，跑自己定的一套帧协议。

现在只做到下位机，上位机还没开始。

## 硬件

主控 STM32F103C8T6，系统时钟 72MHz（HSE 8M 倍 9），APB1 36MHz，APB2 72MHz。

| 外设 | 接口 | 引脚 |
| --- | --- | --- |
| SD 卡 | SPI1 | CS = PA4 |
| VS1053 | SPI2 | XCS = PB6，XDCS = PB9，DREQ = PB7，RST = PB12 |
| 上位机通信 | USART1 | PA9 / PA10，115200 8N1 |
| 音频泵 | TIM3 | 250us 周期中断（72M / 72 / 250） |

VS1053 的 DREQ 是输入脚，拉高表示芯片还能继续吃数据，喂音频时靠它做流控。RST 是低有效。

FATFS 的几个配置点，改之前先看清楚：

1. `FF_CODE_PAGE = 936`，`FF_LFN_UNICODE = 0`，所以 `finfo.fname` 是 GBK 字节流，一个汉字占两字节
2. `FF_USE_LFN = 1`，长文件名打开后会多占 512 字节 BSS
3. `FF_FS_EXFAT = 0`，64G 以上的 SDXC 卡挂不上，会返回 `FR_NO_FILESYSTEM`

## 目录

    Core/       CubeMX 生成的初始化代码
    Drivers/    HAL 库和 CMSIS
    MDK-ARM/    Keil 工程
    FATFS/
        ff.c ffconf.h 等   FatFs R0.16 官方源码，没改过
        sd.c/h             SD 卡 SPI 底层读写
        diskio.c           FatFs 和 sd.c 之间的胶水层
        vs1053.c/h         VS1053 寄存器读写、初始化、音频数据喂送
        proto.c/h          通信协议的组帧和解析
        uart_io.c/h        串口收发环形缓冲，隔开 HAL 和协议层

## 通信协议

一帧的格式，多字节字段都是小端：

| SOF | LEN | CMD | SEQ | PAYLOAD | SUM |
| --- | --- | --- | --- | --- | --- |
| 2 字节 | 2 字节 | 1 字节 | 1 字节 | 0~260 字节 | 1 字节 |

1. SOF：固定 `AA 55`
2. LEN：从 CMD 到 SUM 的总字节数，也就是 `PAYLOAD 长度 + 4`
3. SEQ：序列号，请求和应答靠它对上
4. SUM：从 CMD 开始一直加到 PAYLOAD 结束，取低 8 位，不含 SOF 和 LEN

CMD 的高四位区分方向，`0x10` 开头是下位机主动上报，`0x20` 开头是上位机下发。

| 命令 | 值 | 方向 | 说明 |
| --- | --- | --- | --- |
| CMD_ACK | 0x01 | 下→上 | 应答，`ACK_OK` / `ACK_ERR_CMD` / `ACK_ERR_PAR` / `ACK_ERR_BUSY` / `ACK_ERR_FS` |
| CMD_LIST_BEGIN | 0x10 | 下→上 | 列表开始，载荷放总数 |
| CMD_LIST_ITEM | 0x11 | 下→上 | 单条曲目，载荷放序号 + 文件名 |
| CMD_LIST_END | 0x12 | 下→上 | 列表结束，总数和 BEGIN 不一致就整批作废 |
| CMD_STATUS | 0x13 | 下→上 | 播放状态，14 字节载荷 |
| CMD_EVENT | 0x14 | 下→上 | 事件通知，播完、切歌、SD 出错、没文件 |
| CMD_PONG | 0x15 | 下→上 | PING 的应答 |
| CMD_PING | 0x20 | 上→下 | 联通测试 |
| CMD_LIST_REQ | 0x21 | 上→下 | 要歌曲列表 |
| CMD_PLAY_IDX | 0x22 | 上→下 | 按序号播放 |
| CMD_PAUSE | 0x23 | 上→下 | 暂停 |
| CMD_RESUME | 0x24 | 上→下 | 继续 |
| CMD_STOP | 0x25 | 上→下 | 停止 |
| CMD_NEXT | 0x26 | 上→下 | 下一首 |
| CMD_PREV | 0x27 | 上→下 | 上一首 |
| CMD_SET_VOL | 0x28 | 上→下 | 调音量 |
| CMD_STATUS_REQ | 0x29 | 上→下 | 要当前状态 |

联通测试发 `AA 55 00 00 20 00 20`，应该回 `AA 55 00 00 15 00 15`。

## 串口收发怎么接的

接收用 `HAL_UARTEx_ReceiveToIdle_IT`，HAL 往 64 字节的行缓冲里写，空闲中断触发后
`UART_IO_RxEvent` 把这批字节搬进 256 字节的环形缓冲，然后重新挂接收。主循环调
`UART_IO_Poll` 把环形缓冲里的字节喂给 `PROTO_FeedByte`，凑满一帧且校验通过才回调上层。

环形缓冲里存的只是字节流，不存在"半帧"这个概念，判断帧边界是状态机的事。环满时丢当前字节
而不是覆盖，覆盖会破坏已经在缓冲里的合法数据，丢尾巴至少还能靠校验和发现。

发送侧是 512 字节环形缓冲，协议层组好的帧通过钩子写进来，`Poll` 里分批交给
`HAL_UART_Transmit_IT`。HAL 的中断发送要求内存连续，所以回绕的那一段得分成两次发。

## 进度

做完的：

1. SD 卡 SPI 驱动，FATFS 挂载和读文件
2. VS1053 初始化、SCI 寄存器读写、音频数据喂送
3. `proto.c` 组帧和解析状态机
4. `uart_io.c` 收发环形缓冲

在做：

1. `cmd.c` 命令分发
2. `player.c` 播放状态机
3. `filelist.c` 扫 SD 卡目录

还没动：

1. F407 上位机，LVGL 界面
2. FreeRTOS 移植
3. 上位机中文字库，得是 GB2312 编码的，LVGL 自带的字体只有 ASCII

## 编译

Keil uVision 5，ARMCC 5，C99。打开 `MDK-ARM/C8T6_FATFS.uvprojx` 直接编。

新加的 `.c` 文件要手动加进 Keil 的 FATFS 组，不然编译能过但链接会报未定义符号。

## 还没处理的几个点

1. USART1 中断优先级要在 CubeMX 的 NVIC 页里调得比 TIM3 低，否则串口中断会打断音频泵
2. `HAL_UART_ErrorCallback` 还没写，串口出溢出错误走的是这个回调而不是 `RxEvent`，
   不重新挂接收的话会从此收不到数据
3. 协议层没有重传，收到坏帧只能丢，靠上层 SEQ 对不上再要一次
