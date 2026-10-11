#include "uart_io.h"


extern UART_HandleTypeDef huart1;



static uint8_t s_tx_ring[UART_IO_TX_RING];
static volatile uint16_t s_tx_head;
static volatile uint16_t s_tx_tail;
static volatile uint8_t s_tx_busy;
static volatile uint16_t s_tx_xfer;

static uint8_t s_rx_ring[UART_IO_RX_RING];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
//rx由循环自动进行，不可控，需要先用rx_line接收
static uint8_t s_rx_line[UART_IO_RX_LINE];


static uart_io_frame_cb_t s_frame_cb=(uart_io_frame_cb_t)0;
static uint16_t s_rx_drop;          //诊断用，烧录时注释掉


void UART_IO_Init(void){
    uint16_t i;

    s_tx_head=0;s_tx_tail=0;s_tx_busy=0;s_tx_xfer=0;
    s_rx_head=0;s_rx_tail=0;s_rx_drop=0;
    for(i=0;i<UART_IO_RX_LINE;i++){
        s_rx_line[i]=0;
    }
    HAL_UARTEx_ReceiveToIdle_IT(&huart1,s_rx_line,(uint16_t)UART_IO_RX_LINE);
}

void UART_IO_SetFrameHandler(uart_io_frame_cb_t cb){
    s_frame_cb=cb;
}


//proto.c中发送钩子函数的实现
int8_t   UART_IO_SendHook(const uint8_t *buf, uint16_t len){
    uint16_t i;
    uint16_t used;
    uint16_t free_sp;


    if(s_tx_tail>=s_tx_head){
        used=s_tx_tail-s_tx_head;
    }
    else{
        used=(uint16_t)(UART_IO_TX_RING-s_tx_head+s_tx_tail);
    }
    free_sp=(uint16_t)(UART_IO_TX_RING-used-1);

    if(len>free_sp){
        return -1;          //空闲空间不够，返回-1
    }

    for(i=0;i<len;i++){
        s_tx_ring[s_tx_tail]=buf[i];
        s_tx_tail=(uint16_t)((s_tx_tail+1)%UART_IO_TX_RING);
    }

    return 0;
}

void UART_IO_Poll(void){
    uint16_t n;
    uint16_t cnt;
    uint8_t b;
    int8_t r;

    if(s_tx_busy==0&&s_tx_head!=s_tx_tail){
        if(s_tx_tail>=s_tx_head){
            n=s_tx_tail-s_tx_head;
        }
        else{
            n=UART_IO_TX_RING-s_tx_head;        //hal库需要连续内存，此处不能一次性取完，先取到末尾
        }
        s_tx_xfer=n;
        s_tx_busy=1;
        if(HAL_UART_Transmit_IT(&huart1,&s_tx_ring[s_tx_head],n)!=HAL_OK){
            s_tx_busy=0;
            s_tx_xfer=0;
        }
    }

    cnt=0;
    while(s_rx_head!=s_rx_tail&&cnt<UART_IO_MAX_FEED){
        b=s_rx_ring[s_rx_head];
        s_rx_head=(uint16_t)((s_rx_head+1u)%UART_IO_RX_RING);
        ++cnt;

        r=PROTO_FeedByte(b);
        if(r>0){
            if(s_frame_cb!=(uart_io_frame_cb_t)0){
                s_frame_cb(&g_rx_frame);
            }
        }
    }
}

void UART_IO_TxDone(UART_HandleTypeDef *huart){
    if(huart==(UART_HandleTypeDef*)0){return;}
    if(huart->Instance!=USART1){return;}

    s_tx_head=(uint16_t)((s_tx_head+s_tx_xfer)%UART_IO_TX_RING);
    s_tx_xfer=0;
    s_tx_busy=0;
}

void UART_IO_RxEvent(UART_HandleTypeDef *huart,uint16_t size){
    uint16_t i;
    uint16_t next;
    if(huart==(UART_HandleTypeDef*)0){return;}
    if(huart->Instance!=USART1){return;}
    if(size>UART_IO_RX_LINE){goto restart;}
    for(i=0;i<size;i++){
        next=(uint16_t)((s_rx_tail+1)%UART_IO_RX_RING);
        if(next==s_rx_head){
            s_rx_drop++;
            break;
        }
        s_rx_ring[s_rx_tail]=s_rx_line[i];
        s_rx_tail=next;
    }


restart:
    HAL_UARTEx_ReceiveToIdle_IT(huart,s_rx_line,(uint16_t)UART_IO_RX_LINE);    
}


uint16_t UART_IO_RxPending(void){
    if(s_rx_tail>s_rx_head){return (uint16_t)(s_rx_tail-s_rx_head);}
    return UART_IO_RX_RING-s_rx_tail+s_rx_head;
}


uint16_t UART_IO_TxPending(void){
    if(s_tx_tail>s_tx_head){return (uint16_t)(s_tx_tail-s_tx_head);}
    return UART_IO_TX_RING-s_tx_tail+s_tx_head;
}