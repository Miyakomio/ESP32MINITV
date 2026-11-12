#ifndef _tft_ui_h
#define _tft_ui_h

// TFT 屏幕驱动模块：LVGL 的显示端口
// 负责：初始化屏幕 → 创建 LVGL 显示对象 → 申请画布缓冲 → 把画面刷到屏上
// 用法：任务里先调 tft_ui_init()，然后在循环里周期调用 tft_ui_handler()
//
// 现在这块屏换成了 OpenMV 扩展 LCD（SPI 接口）。

#include <Arduino.h>
#include "lvgl.h"


/* ============================================================
   屏幕型号：只改这一行
     1 = OpenMV 扩展 LCD 的 1.8 寸（ST7735S，128x160）
     0 = OpenMV 扩展 LCD 的 2.4 寸（ILI9341，240x320）
   ============================================================ */
#define TFT_IS_ST7735   1

/* ============================================================
   屏幕接线（按 OpenMV 扩展 LCD 背面丝印接）

     3.3V  →  3V3        （千万不要接 5V）
     GND   →  GND
     CLK   →  GPIO12
     DATA  →  GPIO11
     CS    →  GPIO10
     RS/DC →  GPIO9
     NRST  →  GPIO14
     LED   →  GPIO13     （背光，PWM 调光）

   模块上另一组 P9 / P5 / P4 / P1 / VIN 是给 OpenMV 相机用的，全部悬空。

   ⚠️ 注意：DC 用了 GPIO9，而 power/monitor.h 里的 PMU_SCL 也是 GPIO9，
      两者只能留一个。不用电池监测就把 monitor 任务的 I2C 换到别的脚，
      或者把这里的 TFT_DC_PIN 改掉。
   ============================================================ */
#define TFT_SCLK_PIN   12
#define TFT_MOSI_PIN   11
#define TFT_DC_PIN      9
#define TFT_CS_PIN     10
#define TFT_RST_PIN    14
#define TFT_BL_PIN     13

/* ------------------------------------------------------------
   SPI 写屏时钟：杜邦线接得长时会花屏/闪烁，就从 15MHz 往下调
   （8MHz 最稳，画面会稍慢一点）
   ------------------------------------------------------------ */
#define TFT_SPI_WRITE_HZ   15000000

/* ------------------------------------------------------------
   画面方向（显示不正常时改这两个）

   优先用 TFT_OFFSET_ROTATION 改正画面方向（它是改"面板自己的方向"，
   不会像 setRotation 那样把画面转 90°/变扁）：

     TFT_OFFSET_ROTATION   0 = 正常
                           2 = 上下颠倒 180°   ← 画面上下倒置时用这个
                           1 = 转 90°    3 = 转 270°
                           4~7 = 上面 0~3 再叠加"上下翻转"（镜像）
     TFT_ROTATION          0~3，一般保持 0；只有当你要横屏显示时才改
   ------------------------------------------------------------ */
#define TFT_ROTATION         0      /* 回到"时钟能正常显示"的那一组参数 */
#define TFT_OFFSET_ROTATION  0

/* ------------------------------------------------------------
   偏移自校准（临时用，找对了就关掉）
   打开后：开机自动逐个试 9 组偏移，每组 4 秒，
           屏幕画一圈白框 + 左上角显示编号。
   找到"白框四边都贴边、编号完整可见"的那一组编号后，
   把编号对应的 X/Y 填到 TFT_OFFSET_X / TFT_OFFSET_Y，
   并把 TFT_OFFSET_CALIB 改回 0。
   ------------------------------------------------------------ */
/* 屏幕标定模式：1 = 开机循环播放"分辨率+偏移"组合，用来找出正确参数
   （标定完成后改成 0，并把找到的值填进 TFT_WIDTH/HEIGHT、TFT_OFFSET_X/Y） */
#define TFT_DIAG_MODE        0

/* ============================================================
   分辨率与面板参数
   ============================================================ */
#if TFT_IS_ST7735
  #define TFT_WIDTH       128
  #define TFT_HEIGHT      160
  #define TFT_OFFSET_X    0
  #define TFT_OFFSET_Y    0      /* 图像偏移就试 1 或 2 */
  #define TFT_INVERT      0      /* 颜色反（黑底变白底）就改成 1 */
  #define TFT_RGB_ORDER   1      /* 1 = 面板是 BGR（蓝红对调时用）：青色圆环显示成黄色就是这个 */
#else
  #define TFT_WIDTH       240
  #define TFT_HEIGHT      320
  #define TFT_OFFSET_X    0
  #define TFT_OFFSET_Y    0
  #define TFT_INVERT      0
  #define TFT_RGB_ORDER   0
#endif

void tft_ui_init(void);                  // 初始化（在任务里调用一次）
void tft_ui_handler(void);               // LVGL 心跳（任务循环里周期调用）
void tft_ui_set_brightness(uint8_t v);   // 背光 0~255

/* 直接画一张 JPEG 到屏幕（图传用，不经过 LVGL）
   返回 true = 解码并画成功；false = 数据不是合法 JPEG */
bool tft_ui_draw_jpg(const uint8_t *jpg, uint32_t len, int x, int y);

/* 图传还没收到画面时显示的提示文字 */
void tft_ui_show_msg(const char *msg);

#endif
