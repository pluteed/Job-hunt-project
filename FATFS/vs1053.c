#include "vs1053.h"



static uint8_t VS_SPI_RW_Byte(uint8_t dat){
	uint8_t ret;
	HAL_SPI_TransmitReceive(&hspi2,&dat,&ret,1,HAL_MAX_DELAY);
	return ret;
}

static uint8_t VS_WAITDREQ(uint32_t timeout){
    uint32_t start=HAL_GetTick();
    while(!DREQ_IS_H()){
        if(HAL_GetTick()-start>timeout){
            return 0;
        }
    }
    return 1;
}

void VS_WriteReg(uint8_t addr,uint16_t data){
    if(!VS_WAITDREQ(VS_DREQ_TIMEOUT)) return;
    SCI_SELECT();
    VS_SPI_RW_Byte(0x02);
    VS_SPI_RW_Byte(addr);
    VS_SPI_RW_Byte((uint8_t)(data>>8));
    VS_SPI_RW_Byte((uint8_t)(data&0xFF));
    SCI_DESELECT();
}

uint16_t VS_ReadReg(uint8_t addr){
    uint16_t ret;
    if(!VS_WAITDREQ(VS_DREQ_TIMEOUT)) return 0xFF;
    SCI_SELECT();
    VS_SPI_RW_Byte(0x03);
    VS_SPI_RW_Byte(addr);
    ret=VS_SPI_RW_Byte(0xFF)<<8;
    ret|=VS_SPI_RW_Byte(0xFF);
    SCI_DESELECT();
    return ret;
}

void VS_HardReset(void){
    RST_L();
    HAL_Delay(2);
    RST_H();
    HAL_Delay(5);
    VS_WAITDREQ(500);
}

void VS_SoftReset(void){
    VS_WriteReg(SCI_MODE,SM_RESET|SM_SDINEW);
    HAL_Delay(2);
    VS_WAITDREQ(500);
}

void VS_Init(void){
    XCS_H();
    XDCS_H();
    RST_H();

    __HAL_SPI_DISABLE(&VS_SPI_HANDLE);
    MODIFY_REG(VS_SPI_HANDLE.Instance->CR1,SPI_CR1_BR,SPI_BAUDRATEPRESCALER_32);
    __HAL_SPI_ENABLE(&VS_SPI_HANDLE);

    VS_HardReset();
    VS_SoftReset();

    VS_WriteReg(SCI_MODE,SM_SDINEW|SM_LAYER12);

    if(!VS_TestSPI()){
        while(1) printf("fail\r\n");
    }
    printf("success");

    VS_WriteReg(SCI_CLOCKF,VS_CLOCKF_VALUE);
    HAL_Delay(1);
    VS_WAITDREQ(500);


    __HAL_SPI_DISABLE(&VS_SPI_HANDLE);
    MODIFY_REG(VS_SPI_HANDLE.Instance->CR1,SPI_CR1_BR,SPI_BAUDRATEPRESCALER_32);
    __HAL_SPI_ENABLE(&VS_SPI_HANDLE);


    VS_SetVolume(0x30,0x30);
}                 

void VS_SetVolume(uint8_t left,uint8_t right){
    VS_WriteReg(SCI_VOL,((uint16_t)left<<8|(uint16_t)right));
}






//时钟泵，定时检查dreq电平来进行数据传输，不死等
void VS_PumpInit(void){
    HAL_TIM_Base_Start_IT(&htim3);
}

static const uint8_t *pump_buf =NULL;
static uint16_t       pump_len =0;
static uint16_t       pump_sent=0;



static uint16_t VS_Feed_NonBlock(const uint8_t *buf,uint16_t len){
    uint16_t chunk=len;
    uint16_t i;
    //vs1053一次性只能写入32字节
    if(chunk>32) chunk=32;
    if(chunk==0) return 0;
    if(!DREQ_IS_H()) return 0;

    SDI_SELECT();
    for(i=0;i<chunk;i++){
        VS_SPI_RW_Byte(buf[i]);
    }
    SDI_DESELECT();

    return chunk;
}

//把数据存入静态数组中，重置sent
void VS_PumpSetBuffer(const uint8_t *buf,uint16_t len){
    pump_buf=buf;
    pump_len=len;
    pump_sent=0;
}

void VS_PumpHandler(void){
    uint16_t n;

    if(pump_buf==NULL) return;
    n=VS_Feed_NonBlock(pump_buf+pump_sent,pump_len-pump_sent);
    if(n==0) return;

    pump_sent+=n;

    //已发送字节超过数据长度
    if(pump_sent>=pump_len){
        pump_buf=NULL;
        pump_len=0;
        pump_sent=0;
        VS_BufferEmpty();
    }
}




__weak void VS_BufferEmpty(void){
 
}








uint8_t VS_TestSPI(void){
    uint16_t st=VS_ReadReg(SCI_STATUS);
    return (((st & SS_VER_MASK) >> 4) == 4) ? 1 : 0;
}

void VS_SendData(const uint8_t *buf, uint16_t len)
{
    uint16_t sent  = 0;
    uint16_t chunk;
    uint16_t i;                          /* C90 要求声明在块首（Keil ARMCC5 默认 C90） */

    while (sent < len) {
        chunk = len - sent;
        if (chunk > 32) chunk = 32;      /* 一次最多 32 字节 */

        if (!VS_WAITDREQ(VS_DREQ_TIMEOUT)) return;

        SDI_SELECT();
        for (i = 0; i < chunk; i++) {
            VS_SPI_RW_Byte(buf[sent + i]);
        }
        SDI_DESELECT();

        sent += chunk;
    }
}

// void VS_SineTest(uint8_t n, uint16_t ms)
// {
//     const uint8_t start[8] = {0x53, 0xEF, 0x6E, n, 0x00, 0x00, 0x00, 0x00};
//     const uint8_t stop[8]  = {0x45, 0x78, 0x69, 0x74, 0x00, 0x00, 0x00, 0x00};
//     uint8_t  silence[32];
//     uint8_t  i;
//     uint32_t t0;

//     for (i = 0; i < 32; i++) silence[i] = 0x00;

//     VS_WriteReg(SCI_MODE, SM_SDINEW | SM_TESTS);   /* 开 SDI 测试 */

//     /* 发启动序列（这 8 个字节必须走 SDI，不是 SCI） */
//     if (!VS_WAITDREQ(VS_DREQ_TIMEOUT)) return;
//     SDI_SELECT();
//     for (i = 0; i < 8; i++) VS_SPI_RW_Byte(start[i]);
//     SDI_DESELECT();

//     /* 持续发声。发什么内容无所谓，只要有时钟在跑，这里发 0 保持静默底噪。 */
//     t0 = HAL_GetTick();
//     while (HAL_GetTick() - t0 < ms) {
//         VS_SendData(silence, 32);
//     }

//     /* 发停止序列 */
//     if (!VS_WAITDREQ(VS_DREQ_TIMEOUT)) return;
//     SDI_SELECT();
//     for (i = 0; i < 8; i++) VS_SPI_RW_Byte(stop[i]);
//     SDI_DESELECT();

//     /* 恢复正常工作模式 */
//     VS_WriteReg(SCI_MODE, SM_SDINEW | SM_LAYER12);
// }