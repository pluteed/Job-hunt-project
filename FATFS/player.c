#include "player.h"
#include <string.h>

#include "stm32f1xx_hal.h" 
#include "ff.h"
#include "vs1053.h"   
#include "filelist.h"

//双缓冲，借鉴RTOS中等待列表任务的算法,发送缓冲区空时换指针
static uint8_t s_buf_a[PLAYER_BUF_SIZE];
static uint8_t s_buf_b[PLAYER_BUF_SIZE];

static uint8_t *s_fill;
static uint8_t *s_send;
static uint16_t s_fill_len;   //填充块中的有效字节数

static FIL s_file;
static uint8_t s_file_open;

static uint8_t   s_state;         /* ST_* */
static uint16_t  s_index;
static uint16_t  s_total;
static uint8_t   s_vol;
static uint32_t  s_pos_sec;
static uint32_t  s_dur_sec;

static uint32_t  s_endfill_left;  /* 还剩多少 endFillByte 要灌 */
static uint8_t   s_endfill_byte;
static uint8_t   s_cancel_phase;  /* 0=未复位；1=灌2052；2=已置SM_CANCEL灌32；3=轮询清零 */
static uint32_t  s_cancel_tick;   /* 进入 phase2/3 的起始 tick，用于超时 */
static uint8_t   s_switch_pending; /* 1=收尾完成后要切到 s_pending_idx 而非上报自然结束 */
static uint16_t  s_pending_idx; 
static uint8_t   s_mode;           /* PLAYER_MODE_*：曲终行为（手动/单曲循环/顺序） */
static uint8_t   s_auto_switch;

static uint32_t  s_tick_1s;

#define VOL_ATTN_MAX     254u    /* 衰减上限，255是掉电状态 */
#define VOL_ATTN_DEFAULT 0x30u   /* VS_Init 里 VS_SetVolume(0x30,0x30) 的默认值 */
#define VOL_PCT_DEFAULT  81u     /* 反算：100 - 48*100/254 ≈ 81，与硬件初值对齐 */

static uint8_t Vol_PctToAttn(uint8_t vol){
    if(vol>100u){vol=100u;}
    return (uint8_t)(VOL_ATTN_MAX-((VOL_ATTN_MAX*vol+50)/100));
}


static uint16_t VS_ReadXPara(uint16_t addr){
    VS_WriteReg(SCI_WRAMADDR,addr);
    return VS_ReadReg(SCI_WRAM);
}

static uint8_t ReadEndFillByte(void){
    uint16_t w;
    w=VS_ReadXPara(PARA_ENDFILLBYTE);
    return (uint8_t)(w&0xFF);
}

//实现VS1053中泵空回调
void VS_BufferEmpty(void){
    s_send=0;
}

static void StopPump(void){
    VS_PumpSetBuffer((const uint8_t*)0,0);
    s_send=0;
}

static void CloseFile(void){
    if(s_file_open){
        f_close(&s_file);
        s_file_open=0;
    }
}

static void FinishTrack(void){
    uint8_t pay[EVT_PAY_LEN];
    CloseFile();
    s_state=ST_STOP;
    s_endfill_left=0;
    s_cancel_phase=0;

    pay[EVT_OFF_TYPE]=EVT_TRACK_END;
    PROTO_PutU16(&pay[EVT_OFF_NEWIDX], s_index);
    PROTO_Send(CMD_EVENT, 0, pay, EVT_PAY_LEN);
}

static uint8_t OpenTrack(uint16_t idx){
    char path[PROTO_NAME_MAX+8];
    uint16_t name_len;
    UINT br;
    uint16_t i;
    FRESULT res;

    if(FILELIST_NameByIndex(idx,&path[3],&name_len)==0){
        return ACK_ERR_PAR;
    }

    path[0]='0'; path[1]=':'; path[2]='/';
    path[3+name_len]='\0';

    CloseFile();
    res=f_open(&s_file,path,FA_READ);
    if(res!=FR_OK){
        return ACK_ERR_FS;
    }
    s_file_open=1;

    res=f_read(&s_file,s_fill,PLAYER_BUF_SIZE,&br);
    if(res!=FR_OK){
        CloseFile();
        return ACK_ERR_FS;
    }
    if(br==0){
        CloseFile();
        return ACK_ERR_FS;
    }
    s_fill_len = (uint16_t)br;
    s_index = idx;
    s_pos_sec = 0;
    s_dur_sec = 0;
    s_endfill_left = 0;
    s_endfill_byte = 0;         //刚刚喂入几个字节解码器解析不出文件格式
    s_cancel_phase = 0;

    for(i=0;i<2;i++){VS_WriteReg(SCI_DECODE_TIME,0);}

    return ACK_OK;
}

static void StartSwitch(uint16_t idx, uint8_t auto_switch){
    s_pending_idx=idx;
    s_switch_pending=1u;
    s_auto_switch=auto_switch;
    CloseFile();
    s_endfill_left=PLAYER_ENDFILL;
    s_endfill_byte=ReadEndFillByte();
    s_cancel_phase=1u;
    s_state=ST_PLAYING;
}

static void CompleteCancel(void){
    uint8_t res;
    uint8_t pay[EVT_PAY_LEN];

    s_endfill_left=0;
    s_cancel_phase=0;
    if(s_switch_pending){
        s_switch_pending=0u;
        res=OpenTrack(s_pending_idx);
        s_state=(res==ACK_OK)?ST_PLAYING:ST_ERROR;
        if(res==ACK_OK&&s_auto_switch){
            s_auto_switch=0u;
            pay[EVT_OFF_TYPE]=EVT_AUTO_NEXT;
            PROTO_PutU16(&pay[EVT_OFF_NEWIDX],s_index);
            PROTO_Send(CMD_EVENT,0,pay,EVT_PAY_LEN);
        }
        return;
    }

    if(s_mode==PLAYER_MODE_ONE){
        StartSwitch(s_index,1u);
    }
    else if(s_mode==PLAYER_MODE_SEQ){
        if(s_total==0){
            FinishTrack();
        }
        else{
            StartSwitch((uint16_t)((s_index+1u)%s_total),1u);
        }
    }
}

//外部接口
void PLAYER_Init(void){
    s_fill = s_buf_a;
    s_send = (uint8_t *)0;
    s_fill_len = 0;
    s_file_open = 0;
    s_state = ST_STOP;
    s_index = 0xFFFFu;
    s_total = 0;
    s_vol = VOL_PCT_DEFAULT;      
    s_pos_sec = 0;
    s_dur_sec = 0;
    s_endfill_left = 0;
    s_endfill_byte = 0;
    s_cancel_phase = 0;
    s_cancel_tick = 0;
    s_switch_pending = 0u;
    s_pending_idx = 0u;
    s_mode = PLAYER_MODE_MANUAL;  /* 默认手动：曲终停住，保持原行为兼容 */
    s_auto_switch = 0u;
    s_tick_1s = 0;
}

void PLAYER_SetTotal(uint16_t total)
{
    s_total = total;
}

uint8_t PLAYER_SetMode(uint8_t mode)
{
    if (mode > PLAYER_MODE_SEQ) { return ACK_ERR_PAR; }   /* 仅 0/1/2 合法 */
    s_mode = mode;
    return ACK_OK;
}

uint8_t PLAYER_Play(uint16_t idx)
{
    uint8_t res;

    /* 正在播/暂停中切歌：走 SM_CANCEL 干净收尾，再开新歌（Next/Prev 也走这里） */
    if (s_state == ST_PLAYING || s_state == ST_PAUSE) {
        StartSwitch(idx, 0u);   /* 0u = 用户手动切歌，不报 EVT_AUTO_NEXT */
        return ACK_OK;
    }

    /* 停止/错误态：直接开播 */
    StopPump();
    s_fill = s_buf_a;             /* 重新对齐：填充块从 A 开始 */
    s_fill_len = 0;
    s_cancel_phase = 0;
    s_switch_pending = 0u;

    res = OpenTrack(idx);
    if (res != ACK_OK) {
        s_state = ST_ERROR;
        return res;
    }

    s_state = ST_PLAYING;
    return ACK_OK;
}

uint8_t PLAYER_Pause(void){
    if(s_state!=ST_PLAYING){return ACK_ERR_BUSY;}
    if(s_cancel_phase!=0u){return ACK_ERR_BUSY;}    
    StopPump();
    VS_SetVolume(0xFEu, 0xFEu);   /* 暂停即静音：FIFO 残留仍在解，但被音量档住听不到；0xFE=254 衰减档（非 0xFF 掉电） */
    s_state = ST_PAUSE;
    return ACK_OK;
}

uint8_t PLAYER_Resume(void){
    if(s_state!=ST_PAUSE){return ACK_ERR_BUSY;}
    PLAYER_SetVol(s_vol);                          /* 还原暂停前音量，恢复无缝续播 */
    s_state=ST_PLAYING;
    return ACK_OK;
}

uint8_t PLAYER_Stop(void)
{
    StopPump();
    CloseFile();
    s_fill_len = 0;
    s_endfill_left = 0;
    s_cancel_phase = 0;
    s_switch_pending = 0u;
    s_auto_switch = 0u;           /* 清掉自动切歌标记，避免残留干扰下次播放 */
    s_state = ST_STOP;
    return ACK_OK;
}

uint8_t PLAYER_Next(void)
{
    uint16_t next;

    if (s_total == 0) { return ACK_ERR_FS; }
    next = (uint16_t)(s_index + 1u);
    if (next >= s_total) { next = 0; }
    return PLAYER_Play(next);
}

uint8_t PLAYER_Prev(void)
{
    uint16_t prev;

    if (s_total == 0) { return ACK_ERR_FS; }
    if (s_index == 0 || s_index == 0xFFFFu) {
        prev = (uint16_t)(s_total - 1u);
    } else {
        prev = (uint16_t)(s_index - 1u);
    }
    return PLAYER_Play(prev);
}

uint8_t PLAYER_SetVol(uint8_t vol)
{
    uint8_t attn;

    if (vol > 100u) { vol = 100u; }         /* 越界钳到最大声，别把脏值灌进芯片 */
    attn = Vol_PctToAttn(vol);

    s_vol = vol;                            /* 先记住百分比，状态查询原样回给上位机 */
    if (s_state != ST_PAUSE) {             /* 暂停态不实际写音量：此刻已静音，写了会破静音露出 FIFO 残留 */
        VS_SetVolume(attn, attn);          /* 左右同值；要平衡调节改这里即可 */
    }
    return ACK_OK;
}

uint8_t  PLAYER_GetState(void)   { return s_state; }
uint16_t PLAYER_GetIndex(void)   { return s_index; }
uint32_t PLAYER_GetPosSec(void)  { return s_pos_sec; }
uint32_t PLAYER_GetDurSec(void)  { return s_dur_sec; }
uint8_t  PLAYER_GetVol(void)     { return s_vol; }




/*!?最最最重要的部分!?*/
void PLAYER_Service(void){
    uint16_t n;
    uint16_t i;
    uint16_t mode;
    FRESULT res;
    uint32_t now;
    UINT br;
    uint16_t byterate;

    if(s_state!=ST_PLAYING){
        return;
    }

    //收尾状态机
    if(s_cancel_phase!=0u){
        if(s_send==(uint8_t*)0){
            if(s_endfill_left>0){
                n=((s_endfill_left>PLAYER_BUF_SIZE)?
                                   PLAYER_BUF_SIZE:s_endfill_left);
                for(i=0;i<n;i++){s_fill[i]=s_endfill_byte;}
                VS_PumpSetBuffer(s_fill,n);
                s_send=s_fill;
                s_fill=(s_fill==s_buf_a)?s_buf_a:s_buf_b;
                s_endfill_left-=n;
            }
            else if(s_cancel_phase==1u){
                mode=VS_ReadReg(SCI_MODE);
                VS_WriteReg(SCI_MODE,(uint16_t)(mode|SM_CANCEL));
                s_cancel_phase=2u;
                s_endfill_left=PLAYER_ENDFILL2;
                s_cancel_tick=HAL_GetTick();
            }
            else if(s_cancel_phase==2u){
                s_cancel_phase=3u;
            }
            else{
                mode = VS_ReadReg(SCI_MODE);
                if ((mode & SM_CANCEL) == 0u) {
                    CompleteCancel();             /* 解码器已复位，收尾完成（含切歌则开新歌） */
                    return;
                }
                now = HAL_GetTick();
                if ((uint32_t)(now - s_cancel_tick) > 2000u) {
                    VS_WriteReg(SCI_MODE, (uint16_t)(VS_ReadReg(SCI_MODE) | SM_RESET));
                    CompleteCancel();             /* 超时：软复位兜底（含切歌则开新歌） */
                    return;
                }
            }
        }
        return;
    }

    if(s_send==(uint8_t*)0&&s_fill_len>0){
        VS_PumpSetBuffer(s_fill,s_fill_len);
        s_send = s_fill;
        s_fill = (s_fill == s_buf_a) ? s_buf_b : s_buf_a;   
        s_fill_len = 0;
    }

    if(s_fill_len==0&&s_file_open){
        res=f_read(&s_file,s_fill,PLAYER_BUF_SIZE,&br);
        if(res!=FR_OK){
            s_state=ST_ERROR;
            return;
        }
        if(br>0){
            s_fill_len=(uint16_t)br;
        }
        else{
            CloseFile();
            s_endfill_left = PLAYER_ENDFILL;     /* 先灌 2052 让解码器识别流结束 */
            s_endfill_byte = ReadEndFillByte();  /* 按当前格式重读，避免跨格式用错值 */
            s_cancel_phase = 1u;
        }
    }


    now = HAL_GetTick();
    if ((uint32_t)(now - s_tick_1s) >= 1000u) {
        s_tick_1s = now;
        s_pos_sec = VS_ReadReg(SCI_DECODE_TIME);   /* 芯片自己算的解码秒数 */

        if (s_dur_sec == 0 && s_file_open) {
            byterate = VS_ReadXPara(PARA_BYTERATE);  /* 字节/秒 */
            if (byterate != 0) {
                s_dur_sec = (uint32_t)(f_size(&s_file) / byterate);
            }
        }
    }
} 