#include "cmd.h"
#include "uart_io.h"
#include "player.h"
#include "filelist.h"

//注册收帧回调，由app在初始化时调用一次
void CMD_Init(void){
    UART_IO_SetFrameHandler(CMD_Dispatch);
}

void CMD_SendStatus(uint8_t seq){
    uint8_t pay[ST_PAY_LEN];

    pay[ST_OFF_STATE]=PLAYER_GetState();
    pay[ST_OFF_VOL]=PLAYER_GetVol();
    PROTO_PutU16(&pay[ST_OFF_INDEX],PLAYER_GetIndex());
    PROTO_PutU16(&pay[ST_OFF_TOTAL],FILELIST_GetTotal());
    PROTO_PutU32(&pay[ST_OFF_POS],PLAYER_GetPosSec());
    PROTO_PutU32(&pay[ST_OFF_DUR],PLAYER_GetDurSec());

    PROTO_Send(CMD_STATUS,seq,pay,ST_PAY_LEN);
}

void CMD_Dispatch(const proto_frame_t *f){
    uint16_t idx;

    if(f==(const proto_frame_t*)0){return;}

    if(f->cmd==CMD_PING){
        PROTO_Send(CMD_PONG,f->seq,(const uint8_t*)0,0);
        return;
    }

    if(f->cmd==CMD_LIST_REQ){
        PROTO_SendAck(CMD_LIST_REQ,f->seq,FILELIST_StartSend(f->seq));
        return;
    }

    if(f->cmd==CMD_PLAY_IDX){
        //>PAY_LEN
        if(f->len<PLY_PAY_LEN){
            PROTO_SendAck(CMD_PLAY_IDX,f->seq,ACK_ERR_PAR);
            return;
        }
        idx=PROTO_GetU16(&f->pay[PLY_OFF_INDEX]);
        if(idx>=FILELIST_GetTotal()){
            PROTO_SendAck(CMD_PLAY_IDX,f->seq,ACK_ERR_PAR);
            return;
        }
        PROTO_SendAck(CMD_PLAY_IDX,f->seq,PLAYER_Play(idx));
        return;
    }

    if (f->cmd == CMD_PAUSE) {
        PROTO_SendAck(CMD_PAUSE, f->seq, PLAYER_Pause());
        return;
    }
    if (f->cmd == CMD_RESUME) {
        PROTO_SendAck(CMD_RESUME, f->seq, PLAYER_Resume());
        return;
    }
    if (f->cmd == CMD_STOP) {
        PROTO_SendAck(CMD_STOP, f->seq, PLAYER_Stop());
        return;
    }
    if (f->cmd == CMD_NEXT) {
        PROTO_SendAck(CMD_NEXT, f->seq, PLAYER_Next());
        return;
    }
    if (f->cmd == CMD_PREV) {
        PROTO_SendAck(CMD_PREV, f->seq, PLAYER_Prev());
        return;
    }
    
    if (f->cmd == CMD_SET_VOL){
        if(f->len<VOL_PAY_LEN){
            PROTO_SendAck(CMD_SET_VOL,f->seq,ACK_ERR_PAR);
            return;
        }
        PROTO_SendAck(CMD_SET_VOL,f->seq,PLAYER_SetVol(f->pay[VOL_OFF_VOL]));
        return;
    }
    
    if (f->cmd == CMD_STATUS_REQ) {
        CMD_SendStatus(f->seq);
        return;
    }

    PROTO_SendAck(f->cmd, f->seq, ACK_ERR_CMD);
}
