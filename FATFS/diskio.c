/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2025        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Basic definitions of FatFs */
#include "diskio.h"		/* Declarations FatFs MAI */
#include "sd.h"


/* Example: Mapping of physical drive number for each drive */
//#define DEV_RAM	0	/* Map FTL to physical drive 0 */
//#define DEV_MMC		1	/* Map MMC/SD card to physical drive 1 */
//#define DEV_USB		2	/* Map USB MSD to physical drive 2 */

#define SD_CARD   0
#define SPI_FLASH 1







/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat=STA_NOINIT;
	int result;

	switch (pdrv) {
		case SD_CARD :
			stat=0;
			break;
		default:
			stat=STA_NOINIT;
	}
		return stat;

}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	DSTATUS stat=STA_NOINIT;
	uint8_t result;
	switch (pdrv) {
		case SD_CARD :
			result = SD_CARD_Init();
			if(result == 0)
			{
				stat = 0;
			}
			else
			{
				stat = STA_NOINIT; //?????,??NOINIT
			}
			break;
	
		default:
			stat=STA_NOINIT;
	}
	
	return stat;
}


/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DRESULT res = RES_ERROR;

	switch (pdrv) {
		case SD_CARD:
			if(SD_ReadSector(buff,(uint32_t)sector,(uint16_t)count)==0){
				res=RES_OK;
			}
			break;
		default:
			break;
	}

	return res;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	DRESULT res = RES_ERROR;

	switch (pdrv) {
		case SD_CARD:
			if(SD_WriteSector(buff,(uint32_t)sector,(uint16_t)count)==0){
				res=RES_OK;
			}
		default:
			break;
	}

	return res;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void *buff)
{
	DRESULT res = RES_ERROR;
	if(pdrv != SD_CARD)
	{
		return RES_PARERR;
	}

	switch(cmd)
	{
		case CTRL_SYNC:
			//SPI SD?????,??OK
			res = RES_OK;
			break;

		case GET_SECTOR_COUNT:
			//?SD??????buff
			*(LBA_t *)buff = 3906250;
			res = RES_OK;
			break;

		case GET_SECTOR_SIZE:
			*(WORD *)buff = 512;
			res = RES_OK;
			break;

		case GET_BLOCK_SIZE:
			*(DWORD *)buff = 1;
			res = RES_OK;
			break;

		default:
			res = RES_PARERR;
			break;
	}
	return res;
}



DWORD get_fattime (void)
{

	return (DWORD)(
			(2025 - 1980) << 25 |  // ??:?1980????,?7?
			1 << 21 |              // ??:1,?4?
			1 << 16 |              // ??:1,?5?
			0 << 11 |              // ??:0,?5?
			0 << 5 |               // ??:0,?6?
			0 >> 1                 // ??:0,??2??5?
	);
}