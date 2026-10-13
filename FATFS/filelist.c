#include "filelist.h"
#include <string.h>

#include "ff.h"
#include "proto.h"


#define S_BEGIN             0
#define S_MIDDLE            1
#define S_END               2
#define S_DOWN              3


static FATFS s_fs;
static uint8_t s_mounted;       //是否挂载标志位

static uint16_t s_total;      /* 缓存的曲目总数 */


static uint8_t   s_sending;
static uint8_t   s_phase;     /* 0=发BEGIN 1=发ITEM 2=发END 3=收工 */
static uint8_t   s_seq;
static uint16_t  s_next_idx;

static DIR       s_dir;
static FILINFO   s_finfo;


//内部函数部分
//过滤非音频文件.名
static uint8_t IsAudioName(const char *name){
    const char* dot;
    char ext[5];
    uint16_t i;
    uint16_t n;

    dot=strrchr(name,'.');
    if(dot==(const char*)0){return 0;}

    if(*(dot+1) == '\0'){
        return 0;
    }
    n=0;
    dot++;
    for(i=0;i<4u&&dot[i]!='\0';i++){
        ext[n]=dot[i];
        if(ext[n]>='A'&&ext[n]<='Z'){ext[n]=(char)(ext[n]+32);}
        n++;
    }
    ext[n]='\0';

    if(strcmp(ext,"mp3")==0){return 1;}
    if(strcmp(ext,"wav")==0){return 1;}
    if(strcmp(ext,"wma")==0){return 1;}
    if(strcmp(ext,"mid")==0){return 1;}
    if(strcmp(ext,"ogg")==0){return 1;}
    return 0;
}


//对外接口部分
uint8_t FILELIST_Mount(void){
    FRESULT res;
    if(s_mounted){return 0;}
    res= f_mount(&s_fs,"0:",1);
    if(res!=FR_OK){
        return (uint8_t)res;
    }
    s_mounted=1;
    return 0;
}

uint16_t FILELIST_Count(void){
    DIR dir;
    FILINFO fi;
    FRESULT fr;
    uint16_t cnt;

    if(!s_mounted){return 0;}

    cnt=0;
    fr=f_opendir(&dir,"0:/");
    if(fr!=FR_OK){return 0;}

    while(1){
        fr=f_readdir(&dir,&fi);
        if(fr!=FR_OK||fi.fname[0]=='\0'){break;}
        if(fi.fattrib&AM_DIR){continue;}
        if(fi.fattrib&AM_HID){continue;}
        if(IsAudioName(fi.fname)){++cnt;}
    }

    f_closedir(&dir);
    s_total=cnt;
    return cnt;
}

uint16_t FILELIST_GetTotal(void){
    return s_total;
}

uint8_t FILELIST_NameByIndex(uint16_t idx,char* out,uint16_t *out_len){
    DIR dir;
    FRESULT fr;
    uint16_t cnt;

    if(!s_mounted){return 0;}
    cnt=0;
    fr=f_opendir(&dir,"0:/");
    if(fr!=FR_OK){return 0;}

    while(1){
        fr=f_readdir(&dir,&s_finfo);
        if(fr!=FR_OK||s_finfo.fname[0]=='\0'){break;}
        if(s_finfo.fattrib&AM_DIR){continue;}
        if(s_finfo.fattrib&AM_HID){continue;}
        if(!IsAudioName(s_finfo.fname)){continue;}

        if(idx==cnt){
            *out_len=(uint16_t)strlen(s_finfo.fname);
            memcpy(out,s_finfo.fname,*out_len);
            out[*out_len]='\0';
            f_closedir(&dir);
            return 1;
        }
        cnt++;
    }
    f_closedir(&dir);
    return 0;
}



/*列表项状态机，按照proto格式连发文件信息给上位机*/
/*中断中千万别碰Fatfs接口，不然会串口卡死*/
/*发送列表项不允许丢帧，所以走主循环，每次循环轮寻发送，缓冲区满就等待下一轮*/
uint8_t FILELIST_StartSend(uint8_t seq){
    if(s_sending){return ACK_ERR_BUSY;}
    if(!s_mounted){return ACK_ERR_FS;}

    if (s_total == 0) { FILELIST_Count(); }

    s_sending=1;
    s_phase=S_BEGIN;     /* 0=发BEGIN 1=发ITEM 2=发END 3=收工 */
    s_seq=seq;
    s_next_idx=0;
    return ACK_OK;
}

void FILELIST_Service(void){
    uint8_t pay[IT_OFF_NAME+PROTO_NAME_MAX];
    uint16_t name_len;
    uint16_t i;
    FRESULT res;

    if(!s_sending){return;}

    switch(s_phase){
        case S_BEGIN:
            PROTO_PutU16(&pay[LST_OFF_TOTAL],s_total);
            if(PROTO_Send(CMD_LIST_BEGIN,s_seq,pay,LST_PAY_LEN)<0){return;}

            res=f_opendir(&s_dir,"0:/");
            if(res!=FR_OK){
                PROTO_SendAck(CMD_LIST_REQ,s_seq,ACK_ERR_FS);
                s_sending=0;
                return;
            }
            s_phase=1;
            return;

        case S_MIDDLE:
            while(1){
                res=f_readdir(&s_dir,&s_finfo);
                //错误也跳转首尾，将已经读出的歌曲先传给上位机显示到ui
                if(res!=FR_OK||s_finfo.fname[0]=='\0'){
                    f_closedir(&s_dir);
                    s_phase=S_END;
                    return;
                }
                if(s_finfo.fattrib&AM_DIR){continue;}
                if(s_finfo.fattrib&AM_HID){continue;}
                if(!IsAudioName(s_finfo.fname)){continue;}
                break;
            }

            name_len=(uint16_t)strlen(s_finfo.fname);
            if(name_len>PROTO_NAME_MAX){name_len=PROTO_NAME_MAX;}

            PROTO_PutU16(&pay[IT_OFF_INDEX],s_next_idx);
            for(i=0;i<name_len;i++){pay[IT_OFF_NAME+i]=s_finfo.fname[i];}

            if(PROTO_Send(CMD_LIST_ITEM,s_seq,pay,(uint16_t)(IT_OFF_NAME+name_len))<0){return;}
            s_next_idx++;
            return;
        case S_END:
            PROTO_PutU16(&pay[LST_OFF_TOTAL],s_next_idx);
            if(PROTO_Send(CMD_LIST_END,s_seq,pay,LST_PAY_LEN)<0){return;}
            s_sending=0;
            s_phase=3;
            return;
        default:
            s_sending=0;
            return;
        }
}

uint8_t FILELIST_IsSending(void)
{
    return s_sending;
}
