#ifndef __VS1053_H
#define __VS1053_H
#include "stm32f1xx_hal.h"

/* CubeMX 生成的 SPI 句柄（定义在 spi.c 中，此处只是引用声明） */
extern SPI_HandleTypeDef hspi2;
#define VS_SPI_HANDLE     hspi2


extern TIM_HandleTypeDef htim3;

/* ---------- 引脚定义 ---------- */
#define VS_XCS_PORT       GPIOB
#define VS_XCS_PIN        GPIO_PIN_6      /* SCI 片选：拉低 = 访问寄存器 */
#define VS_XDCS_PORT      GPIOB
#define VS_XDCS_PIN       GPIO_PIN_9
#define VS_DREQ_PORT      GPIOB
#define VS_DREQ_PIN       GPIO_PIN_7      /* 输入：高 = 芯片还能吃数据 */
#define VS_RST_PORT       GPIOB
#define VS_RST_PIN        GPIO_PIN_12      /* 输出：低电平复位（注意是低有效） */

/* ============================================================================
 * SCI 寄存器地址（手册 §8.7）—— 每次访问都是「指令字节 + 地址 + 16 位数据」
 * ========================================================================== */
#define SCI_MODE          0x00    /* 模式控制：片选方式、复位、解码使能、测试等 */
#define SCI_STATUS        0x01    /* 状态：版本号 SS_VER、是否允许跳转等 */
#define SCI_BASS          0x02    /* 高低音增强（Treble/Bass） */
#define SCI_CLOCKF        0x03    /* 时钟倍频：决定 CLKI 跑多快 */
#define SCI_DECODE_TIME   0x04    /* 已解码秒数（播放时间） */
#define SCI_AUDATA        0x05    /* 采样率与声道：bits[15:1]=采样率/2，bit0=立体声 */
#define SCI_WRAM          0x06    /* 间接寻址窗口，配 WRAMADDR 用（烧 patch） */
#define SCI_WRAMADDR      0x07    /* WRAM 地址，写完自动递增 */
#define SCI_HDAT0         0x08    /* 文件头魔数低 16 位：格式、声道、采样率 */
#define SCI_HDAT1         0x09    /* 文件头魔数高 16 位：比特率等 */
#define SCI_AIADDR        0x0A    /* 用户程序（patch）入口地址 */
#define SCI_VOL           0x0B    /* 音量：0x00 最大，0xFE 静音，0xFF 模拟掉电 */
#define SCI_AICTRL0       0x0C    /* 用户程序控制字 0 */
#define SCI_AICTRL1       0x0D    /* 用户程序控制字 1 */
#define SCI_AICTRL2       0x0E    /* 用户程序控制字 2 */
#define SCI_AICTRL3       0x0F    /* 用户程序控制字 3 */

/* ============================================================================
 * SCI_MODE 位定义（手册 §8.7.1）
 * ========================================================================== */
#define SM_DIFF           0x0001  /* 立体声反相输出（单声道差分时可得 3 Vpp） */
#define SM_LAYER12        0x0002  /* 允许解码 MPEG Layer I / II（默认只开 Layer III） */
#define SM_RESET          0x0004  /* 软件复位：写 1 后自清 */
#define SM_CANCEL         0x0008  /* 取消当前解码：写完要继续喂数据等它自清 */
#define SM_SDISHARE       0x0010  /* 共享片选：1 = XDCS 由 XCS 内部反相产生 */
#define SM_TESTS          0x0020  /* 允许 SDI 测试（正弦测试必须开） */
#define SM_STREAM         0x0040  /* 流模式（VS1053 上基本不用） */
#define SM_SDINEW         0x0800  /* VS1002 原生新片选语义，复位默认 1，必须保持 */
#define SM_DACT           0x1000  /* DCLK 有效边沿取反 */
#define SM_ADPCM_HP       0x4000  /* 录音：ADPCM 高通滤波 */
#define SM_LINE1          0x4000  /* 录音输入选择：LINE1 而非 MIC */
#define SM_CLK_RANGE      0x8000  /* 输入时钟 24~26 MHz 时置 1 */

/* ============================================================================
 * SCI_STATUS 位定义（手册 §8.7.2）
 * ========================================================================== */
#define SS_REFERENCE_SEL  0x0001  /* 参考电压选择：1 = 1.65 V（需 AVDD ≥ 3.3 V） */
#define SS_AD_CLOCK       0x0002  /* AD 调制时钟：0 = 6 MHz，1 = 3 MHz */
#define SS_APDOWN1        0x0004  /* 内部模拟掉电 */
#define SS_APDOWN2        0x0008  /* 模拟驱动掉电 */
#define SS_VER_MASK       0x00F0  /* [7:4] 芯片版本号，VS1053 = 4 */
#define SS_VCM_DISABLE    0x0400  /* 关闭 GBUF 过载保护 */
#define SS_VCM_OVERLOAD   0x0800  /* GBUF 过载指示（读到 1 = 短路到地了） */
#define SS_SWING_MASK     0x7000  /* [14:12] 摆幅增益，0/1/2 = +0/+0.5/+1.0 dB */
#define SS_DO_NOT_JUMP    0x8000  /* 正在解析文件头，禁止快进快退 */

/* ============================================================================
 * Extra Parameter 地址（存在 X RAM，需经 WRAMADDR/WRAM 间接访问）
 * ========================================================================== */
#define PARA_PLAYSPEED    0x1E04  /* 倍速播放：1 = 正常，2 = 2 倍速 */
#define PARA_BYTERATE     0x1E05  /* 平均字节率，bitrate = byteRate × 8 */
#define PARA_ENDFILLBYTE  0x1E06  /* 流结束时必须灌入的填充字节 */
#define PARA_POSOFFSETHZ  0x1E07  /* 采样率微调（Hz） */

/* ============================================================================
 * CLOCKF 常用值（手册推荐 SC_MULT = 3.5×，SC_ADD = 1.0×）
 *   0x8800 = SC_MULT=4(→3.5×) + SC_ADD=1(+1.0×)  CLKI = 12.288 × 3.5 = 43.008 MHz
 *   0x6000 = SC_MULT=3(→3.0×) + SC_ADD=0         CLKI = 36.864 MHz（保守，够解 MP3）
 *   注意 SC_MULT 是查表索引而非倍率：n≥1 时倍率 = 1.5 + 0.5n
 * ========================================================================== */

#define CLOCKF_35X        0x8800

#define VS_DREQ_TIMEOUT   100

/* 本驱动实际使用的时钟档位 */
#define VS_CLOCKF_VALUE   CLOCKF_35X

#define XCS_L()           HAL_GPIO_WritePin(VS_XCS_PORT,VS_XCS_PIN,GPIO_PIN_RESET)
#define XCS_H()           HAL_GPIO_WritePin(VS_XCS_PORT,VS_XCS_PIN,GPIO_PIN_SET)
#define XDCS_L()          HAL_GPIO_WritePin(VS_XDCS_PORT,VS_XDCS_PIN,GPIO_PIN_RESET)
#define XDCS_H()          HAL_GPIO_WritePin(VS_XDCS_PORT,VS_XDCS_PIN,GPIO_PIN_SET)
#define RST_L()           HAL_GPIO_WritePin(VS_RST_PORT,VS_RST_PIN,GPIO_PIN_RESET)
#define RST_H()           HAL_GPIO_WritePin(VS_RST_PORT,VS_RST_PIN,GPIO_PIN_SET)
#define DREQ_IS_H()       (HAL_GPIO_ReadPin(VS_DREQ_PORT,VS_DREQ_PIN)==GPIO_PIN_SET)

#define SCI_SELECT()      XCS_L()
#define SCI_DESELECT()    XCS_H()
#define SDI_SELECT()      XDCS_L()
#define SDI_DESELECT()    XDCS_H()



void VS_WriteReg(uint8_t addr,uint16_t data);
uint16_t VS_ReadReg(uint8_t addr);
void VS_HardReset(void);
void VS_SoftReset(void);
void VS_Init(void);
void VS_SetVolume(uint8_t left,uint8_t right);


void VS_PumpSetBuffer(const uint8_t *buf,uint16_t len);
void VS_PumpHandler(void);
__weak void VS_BufferEmpty(void);
uint8_t VS_TestSPI(void);


//void VS_SendData(const uint8_t *buf, uint16_t len);




//void VS_SineTest(uint8_t n, uint16_t ms);
#endif
