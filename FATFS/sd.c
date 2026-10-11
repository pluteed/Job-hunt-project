#include "sd.h"


extern SPI_HandleTypeDef hspi1;

static uint8_t SD_SPI_RW_Byte(uint8_t dat){
	uint8_t ret;
	HAL_SPI_TransmitReceive(&hspi1,&dat,&ret,1,HAL_MAX_DELAY);
	return ret;
}

static uint8_t SD_SendCMDWaitResponse(uint8_t *resp,uint32_t timeout){
	uint8_t val;
	while(timeout--){
		val=SD_SPI_RW_Byte(0xFF);
		if((val&0x80)==0){
			*resp=val;
			return 0;
		}
	}
	return 1;
}

static uint8_t SD_SendCmd(uint8_t cmd,uint32_t arg,uint8_t crc){
	uint8_t r1;
	
	SD_SPI_RW_Byte(cmd|0x40);
	SD_SPI_RW_Byte((arg>>24)&0xFF);
	SD_SPI_RW_Byte((arg>>16)&0xFF);
	SD_SPI_RW_Byte((arg>>8)&0xFF);
	SD_SPI_RW_Byte(arg&0xFF);
	SD_SPI_RW_Byte(crc);
	
	if(SD_SendCMDWaitResponse(&r1,0x1000)!=0){
		r1=0xFF;
	}
	
	/*【修复2】原来取完 R1 后还有一句多余的 SD_SPI_RW_Byte(0xFF)，
	会顶掉命令的后续响应字节，已删除。*/
	return r1;
}


static uint8_t SD_ReadOneSector(uint8_t *pbuf,uint32_t LBA){
	uint16_t i;
	uint8_t r1;
	uint16_t timeout=0xFFFF;
	
	SD_CS_LOW();
	r1=SD_SendCmd(17, LBA*512, 0xFF);
	if(r1!=0){
		SD_CS_HIGH();
		return 1;
	}
	
	while(SD_SPI_RW_Byte(0xFF)!=0xFE){
		timeout--;
		if(timeout==0){
			SD_CS_HIGH();
			return 1;
		}
	}
	for(i=0;i<512;i++){
		pbuf[i]=SD_SPI_RW_Byte(0xFF);
	}
	
	SD_SPI_RW_Byte(0xFF);
	SD_SPI_RW_Byte(0xFF);
	
	SD_CS_HIGH();
	return 0;
}

static uint8_t SD_WriteOneSector(const uint8_t *pbuf,uint32_t LBA){
	uint16_t i;
	uint8_t r1;
	uint16_t timeout;
	
	SD_CS_LOW();
	r1=SD_SendCmd(24, LBA*512, 0xFF);
	if(r1!=0){
		SD_CS_HIGH();
		return 1;
	}
	
	timeout=0xFFFF;
	while(SD_SPI_RW_Byte(0xFF)!=0xFF){
		timeout--;
		if(timeout==0){
			SD_CS_HIGH();
			return 1;
		}
	}
	
	SD_SPI_RW_Byte(0xFE);
	for(i=0;i<512;i++){
		SD_SPI_RW_Byte(pbuf[i]);
	}
	SD_SPI_RW_Byte(0xFF);
	SD_SPI_RW_Byte(0xFF);
	
	/* wait for data response token: skip Ncr idle bytes until non-0xFF */
	for (timeout = 0xFFFF; (r1 = SD_SPI_RW_Byte(0xFF)) == 0xFF; ) {
		if (timeout-- == 0) {
			SD_CS_HIGH();
			return 1;
		}
	}
	if ((r1 & 0x1F) != 0x05) {
		SD_CS_HIGH();
		return 1;
	}
	
	timeout = 0xFFFF;
	while(SD_SPI_RW_Byte(0xFF) == 0x00)
	{
		timeout--;
		if(timeout ==0)
		{
			SD_CS_HIGH();
			return 1;
		}
	}
	
	SD_CS_HIGH();
	return 0;
}

uint8_t SD_CARD_Init(void)
{
	uint8_t r1;
	uint16_t i;
	uint16_t timeout;

	// 固定低速256分频
	__HAL_SPI_DISABLE(&hspi1);
	hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
	if(HAL_SPI_Init(&hspi1) != HAL_OK)
	{
		printf("SPI slow init fail\r\n");
		SD_CS_HIGH();
		for(i=0;i<5;i++) SD_SPI_RW_Byte(0xFF);
		return 0xFF;
	}
	__HAL_SPI_ENABLE(&hspi1);

	// PA4/CS 已由 CubeMX 的 MX_GPIO_Init() 配置为推挽输出、初始高电平，这里不再重复初始化

	SD_CS_HIGH();
	for(i=0;i<10;i++){
		SD_SPI_RW_Byte(0xFF);
	}
	HAL_Delay(100);

	//==== CMD0 ====
	timeout=200;
	do
	{
		SD_CS_LOW();
		r1 = SD_SendCmd(0,0,0x95);
		SD_CS_HIGH();
		printf("CMD0 r1=%d\r\n",r1);
		timeout--;
	/*【修复1】原来判 (r1 & 0x01)，0xFF 的 bit0 也是 1，无响应会被当成进入 idle 成功。严格判 ==0x01，否则失败会被日志掩盖。*/
	}while( (r1 != 0x01) && timeout>0 );

	if(r1 != 0x01)
	{
		printf("CMD0 fail, no response! check CS(PA4)/MISO/3.3V\r\n");
		SD_CS_HIGH();
		for(i=0;i<5;i++) SD_SPI_RW_Byte(0xFF);
		return 1;
	}
	printf("CMD0 success, enter idle\r\n");

	//==== SDSC(老卡) 初始化：CMD55 + ACMD41 循环，CMD1 仅作 MMC 兜底 ====
	printf("run ACMD41\r\n");
	timeout = 0xFFFF;
	do
	{
		SD_CS_LOW();
		SD_SendCmd(55,0,0xFF);
		r1 = SD_SendCmd(41, 0, 0xFF);
		SD_CS_HIGH();
		timeout--;
		printf("ACMD41 r1=%d, timeout=%d\r\n",r1,timeout);
	}while(r1 == 0x01 && timeout>0);

	if(r1 == 0x05 || r1 == 0xFF)
	{
		/* ACMD41 不支持，再尝试 MMC 的 CMD1 */
		printf("ACMD41 not supported, try MMC CMD1\r\n");
		timeout = 0xFFFF;
		do
		{
			SD_CS_LOW();
			r1 = SD_SendCmd(1, 0, 0xFF);
			SD_CS_HIGH();
			timeout--;
			printf("CMD1 r1=%d, timeout=%d\r\n", r1, timeout);
		} while(r1 == 0x01 && timeout > 0);
	}

	if(r1 != 0x00)
	{
		printf("SD init FAIL, r1=%d\r\n", r1);
		SD_CS_HIGH();
		for(i=0;i<5;i++) SD_SPI_RW_Byte(0xFF);
		return 3;
	}
	printf("SDSC init OK!\r\n");

	// 退出前清理总线
	SD_CS_HIGH();
	for(i=0;i<5;i++) SD_SPI_RW_Byte(0xFF);
	HAL_Delay(20);

	return 0;
}


uint8_t SD_ReadSector(uint8_t *pbuf, uint32_t LBA, uint8_t cnt){
	uint8_t i;
	for(i=0;i<cnt;i++){
		if(SD_ReadOneSector(pbuf,LBA+i)!=0){
			return 1;
		}
		pbuf+=512;
	}
	return 0;
}


uint8_t SD_WriteSector(const uint8_t *pbuf, uint32_t LBA, uint8_t cnt){
	uint8_t i;
	for(i = 0; i < cnt; i++)
	{
			if( SD_WriteOneSector(pbuf, LBA + i) != 0 )
			{
					printf("SD_WriteSector fail LBA=%lu cnt=%u\r\n", LBA + i, (unsigned)cnt);
				return 1;
			}
			pbuf += 512;
		}
	return 0;
}