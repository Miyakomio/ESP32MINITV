#include "freertos_run.h"

/* ============================================================
   ★★★ 一个宏换整套功能：只改下面一行 ★★★

       MODE_CLOCK    = 时钟界面（WiFi 连网 + NTP 对时 + 信号格 + 背景图）
       MODE_IMAGERX  = 串口图传（接收 OpenMV 通过 UART 发来的 JPEG 并显示）

   两套代码都完整保留在本文件里，用 #if 决定谁参与编译。
   另一套会被"无视"，不进固件、不占 Flash；切回来只改这一行。
   ============================================================ */
#define MODE_CLOCK      1
#define MODE_IMAGERX    2

#define MODE            MODE_CLOCK      /* <<< 改这里切换 */

/* ============================================================
   任务栈大小
   ============================================================ */
#define STACK_DISPLAY   8192      /* LVGL + 整屏刷图很吃栈，4096 容易栈溢出导致反复重启 */
#define STACK_WIFI      4096

static TaskHandle_t  s_displayHandle = NULL;

/* ============================================================
   模式 1：时钟界面（原代码，一字未删）
   ============================================================ */
#if (MODE == MODE_CLOCK)

#include "WIFI/wifilink.h"
#include <time.h>

/* 表盘背景图：定义在 src/bg_clock.c（128x160，RGB565，由图片裁切缩放生成） */
extern const lv_image_dsc_t bg_clock;

static TaskHandle_t   s_wifiHandle = NULL;
static QueueHandle_t  s_netQueue   = NULL;

/* ============================================================
   显示任务：全工程唯一允许操作 LVGL 的地方
   界面 = 手表时钟：秒环 + 大号时间 + 秒数
   ============================================================ */
static lv_obj_t * s_ring     = NULL;   /* 秒环 */
static lv_obj_t * s_lbl_time = NULL;   /* 大号时间 */
static lv_obj_t * s_lbl_sec  = NULL;   /* 秒数 */

/* 小屏（128x160）用 28 号，大屏（240x320）用 40 号
   两个字体都在 include/lv_conf.h 和 platformio.ini 的 build_flags 里打开了 */
#if TFT_WIDTH <= 160
  #define CLOCK_FONT (&lv_font_montserrat_28)
#else
  #define CLOCK_FONT (&lv_font_montserrat_40)
#endif

/* 没有 RTC：用"起始时间 + millis()"走时，上电从 10:08:00 开始 */
static uint32_t s_start_sec  = 10 * 3600 + 8 * 60;
static uint32_t s_elapsed_ms = 0;
static uint32_t s_last_ms    = 0;

/* 网络状态：WiFi 任务 → 显示任务 */
typedef struct {
    bool connected;    /* WiFi 是否连上 */
    int  rssi;         /* dBm，未连接时无效 */
    bool time_synced;  /* NTP 是否对时成功 */
} net_info_t;

/* 给网络任务预留：对时完成或手动改时间时调用 */
void clock_set(int hour, int min, int sec)
{
    s_start_sec  = (uint32_t)(hour % 24) * 3600 + (uint32_t)(min % 60) * 60 + (uint32_t)(sec % 60);
    s_elapsed_ms = 0;
    s_last_ms    = millis();
}

static lv_obj_t * s_bar[4] = { NULL, NULL, NULL, NULL };
static void ui_create(void)
{
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b1015), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- 背景图片（先建 = 在最底层）---- */
    lv_obj_t * bg = lv_image_create(scr);
    lv_image_set_src(bg, &bg_clock);
    lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    /* ---- 压暗层：保证时间文字在图片上依然清晰（想更亮/更暗就改 LV_OPA_xx）---- */
    lv_obj_t * dim = lv_obj_create(scr);
    lv_obj_set_size(dim, TFT_WIDTH, TFT_HEIGHT);
    lv_obj_align(dim, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dim, LV_OPA_50, 0);       /* 50% 黑，可调 */
    lv_obj_set_style_border_width(dim, 0, 0);
    lv_obj_set_style_radius(dim, 0, 0);
    lv_obj_set_style_pad_all(dim, 0, 0);
    lv_obj_remove_flag(dim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dim, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 4; i++) {
        s_bar[i] = lv_obj_create(scr);                 /* 用普通矩形对象当柱子 */
        lv_obj_set_size(s_bar[i], 3, 4 + i * 2);       /* 宽 3px；高 4/6/8/10px */
        lv_obj_set_style_radius(s_bar[i], 1, 0);
        lv_obj_set_style_border_width(s_bar[i], 0, 0); /* 去掉主题默认边框 */
        lv_obj_set_style_pad_all(s_bar[i], 0, 0);
        lv_obj_remove_flag(s_bar[i], LV_OBJ_FLAG_SCROLLABLE);
        /* 右上角：最右边那根最高；四根底边对齐在 y=12 */
        lv_obj_align(s_bar[i], LV_ALIGN_TOP_RIGHT,
                    -4 - (3 - i) * 5,             /* x：从右往左，间距 5px */
                    2 + (10 - (4 + i * 2)));      /* y：让底边齐平 */
    }
    const int d = (TFT_WIDTH < TFT_HEIGHT ? TFT_WIDTH : TFT_HEIGHT) - 14;

    /* 秒环：每走一秒前进一格 */
    s_ring = lv_arc_create(scr);
    lv_obj_set_size(s_ring, d, d);
    lv_arc_set_range(s_ring, 0, 60);
    lv_arc_set_value(s_ring, 0);
    lv_obj_align(s_ring, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_style(s_ring, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(s_ring, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(0x1b2733), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(0x35c1ff), LV_PART_INDICATOR);

    /* 大号时间 */
    s_lbl_time = lv_label_create(scr);
    lv_label_set_text(s_lbl_time, "12:00 ");
    lv_obj_set_style_text_font(s_lbl_time, CLOCK_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_lbl_time, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(s_lbl_time, LV_ALIGN_CENTER, 0, -14);

    /* 秒数 */
    s_lbl_sec = lv_label_create(scr);
    lv_label_set_text(s_lbl_sec, "00");
    lv_obj_set_style_text_font(s_lbl_sec, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_lbl_sec, lv_color_hex(0x35c1ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(s_lbl_sec, LV_ALIGN_CENTER, 0, 18);
}

static int rssi_to_bars(int rssi, bool connected)//网络信号强度绘制
{
    if (!connected || rssi >= 0) return 0;   /* 没连上：0 格 */
    if (rssi > -60) return 4;
    if (rssi > -70) return 3;
    if (rssi > -80) return 2;
    return 1;
}

/* 每 200ms 调一次 */
static void clock_update(void)
{
    const uint32_t now = millis();
    s_elapsed_ms += (now - s_last_ms);
    s_last_ms = now;

    const uint32_t total = s_start_sec + s_elapsed_ms / 1000;
    const int sec  = total % 60;
    const int min  = (total / 60) % 60;
    const int hour = (total / 3600) % 24;

    lv_label_set_text_fmt(s_lbl_time, "%02d:%02d", hour, min);
    lv_label_set_text_fmt(s_lbl_sec, "%02d", sec);
    lv_arc_set_value(s_ring, sec);
}

/* ============================================================
   网络任务：连 WiFi → NTP 对时 → 每小时再对一次
   对时成功后调用 clock_set()，显示任务下一帧就会用上正确时间
   ============================================================ */
static void wifiTask(void * pvParam)
{
    (void)pvParam;

    wifistartlink();                       /* 开始连接（不阻塞） */

    /* 等它连上，最多等 15 秒 */
    for (int i = 0; i < 30 && !wifi_is_connected(); i++) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (wifi_is_connected()) {
        Serial.printf("[WIFI] 已连接  IP: %s\r\n", WiFi.localIP().toString().c_str());
        /* 东八区（中国无夏令时），NTP 服务器用国内能访问的 */
        configTime(8 * 3600, 0, "ntp.aliyun.com", "cn.pool.ntp.org", "pool.ntp.org");
        Serial.println("[NTP] 开始对时…");
    } else {
        Serial.println("[WIFI] 没连上（检查 wifilink.h 里的名称/密码），稍后自动重试");
    }

    for (;;) {
        const bool conn = wifi_is_connected();
        if (!conn) {
            reconnectwifi();
        } else {
            struct tm t = {};
            if (getLocalTime(&t, 2000)) {          /* NTP 同步过之后这里就有值 */
                clock_set(t.tm_hour, t.tm_min, t.tm_sec);
                Serial.printf("[NTP] 对时完成: %02d:%02d:%02d\r\n",
                              t.tm_hour, t.tm_min, t.tm_sec);
            } else {
                Serial.println("[NTP] 还没拿到时间，下次再试");
            }
        }
        net_info_t net = {};
        net.connected   = conn;
        net.rssi        = conn ? WiFi.RSSI() : 0;
        net.time_synced = true;
        if (s_netQueue) xQueueOverwrite(s_netQueue, &net);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

#endif /* MODE_CLOCK */

/* ============================================================
   模式 2：串口图传（OpenMV --UART--> ESP32 --> 屏幕）

   协议：[0xAA][0x55][len_hi][len_lo][JPEG 数据...][checksum]

   接线：OpenMV P4(TX) → IMG_UART_RX_PIN
         OpenMV P5(RX) → IMG_UART_TX_PIN（只接收可不接）
         GND 必须共地
   ============================================================ */
#if (MODE == MODE_IMAGERX)

/* 你的接线：OpenMV 的 TX → ESP32 的 GPIO18（接收脚） */
#define IMG_UART_RX_PIN   18
#define IMG_UART_TX_PIN   17                /* 只接收，这根用不到 */
/* 曾把这里降到 460800 想解决丢包，但真正的根因是"收字节和画屏在同一个任务里"，
   现在收字节已经拆成独立的 imageRxTask，先按 921600 跑（和 OpenMV 的 UART(1, 921600) 对应）。
   如果 921600 还是丢包，就把它和 OpenMV 一起降到 460800 或 230400。 */
#define IMG_UART_BAUD     921600
#define IMG_FRAME_MAX     (24 * 1024)     /* 单帧 JPEG 上限，超了就丢帧重同步 */

/* 诊断开关：
     1 = 正常：把收到的画面画到屏幕上
     0 = 只收不画：屏幕会停在 WAIT IMAGE，用来单独测"串口接收能力"
          （如果关掉画屏后收到的字节数暴涨，说明是画屏把接收饿死了） */
#define IMG_SHOW_ON_SCREEN   1

#define MAGIC_0   0xAA
#define MAGIC_1   0x55

/* 收帧状态机 */
enum { ST_HUNT = 0, ST_MAGIC1, ST_LEN_H, ST_LEN_L, ST_DATA, ST_SUM };

static uint8_t * s_rx_buf  = NULL;    /* 正在接收的帧 */
static uint8_t * s_rdy_buf = NULL;    /* 收好、等待显示的帧（最新一帧覆盖旧的） */

static uint8_t s_rx_static[IMG_FRAME_MAX];     /* PSRAM 申请失败时的退路 */
static uint8_t s_rdy_static[IMG_FRAME_MAX];

static volatile bool     s_rdy_flag = false;
static volatile uint32_t s_rdy_len  = 0;

static int      s_state        = ST_HUNT;
static uint32_t s_payload_len  = 0;             /* 本帧 JPEG 长度 */
static uint32_t s_rx_len       = 0;             /* 数据区已收字节数 */
static uint32_t s_last_byte_ms = 0;             /* 最后一个字节的时间（超时重同步用） */

static uint32_t s_frames = 0, s_bytes = 0, s_drops = 0, s_sum_err = 0;
static uint32_t s_drawn  = 0;           /* 实际画到屏幕上的帧数 */
static uint32_t s_draw_us = 0, s_draw_n = 0;   /* 画一帧的耗时（微秒） */
static uint32_t s_total_bytes = 0;      /* 收到的原始字节总数（含被丢弃的） */
static uint32_t s_dumped = 0;           /* 已打印的原始字节数（调试用） */

static TaskHandle_t s_imgRxHandle = NULL;
static void imageRxTask(void *pvParam);      /* 独立收字节任务（前向声明） */

static void image_rx_init(void)
{
    s_rx_buf  = (uint8_t *)heap_caps_malloc(IMG_FRAME_MAX, MALLOC_CAP_SPIRAM);
    s_rdy_buf = (uint8_t *)heap_caps_malloc(IMG_FRAME_MAX, MALLOC_CAP_SPIRAM);
    if (!s_rx_buf || !s_rdy_buf) {
        s_rx_buf  = s_rx_static;
        s_rdy_buf = s_rdy_static;
        Serial.println("[IMG] PSRAM 不足，改用静态缓冲");
    } else {
        Serial.printf("[IMG] 缓冲来自 PSRAM，剩余 %u 字节\r\n", (unsigned)ESP.getFreePsram());
    }

    Serial1.setRxBufferSize(8192);              /* 必须在 begin() 之前 */
    Serial1.begin(IMG_UART_BAUD, SERIAL_8N1, IMG_UART_RX_PIN, IMG_UART_TX_PIN);

    Serial.printf("[IMG] uart1  RX=%d  TX=%d  baud=%d  单帧上限=%d 字节\r\n",
                  IMG_UART_RX_PIN, IMG_UART_TX_PIN, IMG_UART_BAUD, IMG_FRAME_MAX);

    tft_ui_show_msg("WAIT IMAGE");
    s_state = ST_HUNT;
    s_last_byte_ms = millis();

    /* 收字节必须单独一个任务，而且要抢在显示任务前面跑。
       因为显示任务解码 + 画一张 JPEG 要阻塞几十毫秒，
       如果同时由它来读串口，UART 的 128 字节硬件 FIFO 和驱动缓冲会溢出，
       实测会丢掉约 90% 的数据（现象就是"帧永远凑不齐、校验错慢慢涨"）。 */
    xTaskCreatePinnedToCore(imageRxTask, "imgrx", 4096, NULL, 4, &s_imgRxHandle, 1);
}

static void image_rx_poll(void)
{
    while (Serial1.available()) {
        const uint8_t b = (uint8_t)Serial1.read();
        s_last_byte_ms = millis();
        s_total_bytes++;

        /* 调试：把开机后收到的前 64 个原始字节打出来
           如果数据正确，应该能在里面看到：AA 55 07 15 FF D8 FF ... */
        if (s_dumped < 64) {
            Serial.printf("%02X ", b);
            if (++s_dumped % 16 == 0) Serial.println();
        }

        switch (s_state) {
        case ST_HUNT:
            if (b == MAGIC_0) s_state = ST_MAGIC1;
            break;

        case ST_MAGIC1:
            if (b == MAGIC_1)      s_state = ST_LEN_H;
            else if (b != MAGIC_0) s_state = ST_HUNT;    /* 容忍 AA AA 55 */
            break;

        case ST_LEN_H:
            s_payload_len = ((uint32_t)b) << 8;
            s_state = ST_LEN_L;
            break;

        case ST_LEN_L:
            s_payload_len |= b;
            if (s_payload_len == 0 || s_payload_len > IMG_FRAME_MAX) {
                s_drops++;
                s_state = ST_HUNT;                        /* 长度不合理，重新找帧头 */
            } else {
                s_rx_len = 0;
                s_state  = ST_DATA;
            }
            break;

        case ST_DATA:
            s_rx_buf[s_rx_len++] = b;
            if (s_rx_len >= s_payload_len) s_state = ST_SUM;
            break;

        case ST_SUM: {
            uint8_t sum = 0;
            for (uint32_t i = 0; i < s_payload_len; i++) sum += s_rx_buf[i];
            /* 调试：前 5 帧打印长度、JPEG 头、校验和对比 */
            if (s_frames + s_sum_err < 5) {
                Serial.printf("[IMG] len=%u  头=%02X %02X %02X %02X  算出=%02X 收到=%02X  %s\r\n",
                              (unsigned)s_payload_len,
                              s_rx_buf[0], s_rx_buf[1], s_rx_buf[2], s_rx_buf[3],
                              sum, b, (sum == b) ? "OK" : "FAIL");
            }
            if (sum == b) {
                memcpy(s_rdy_buf, s_rx_buf, s_payload_len);   /* 交给显示侧 */
                s_rdy_len  = s_payload_len;
                s_rdy_flag = true;
                s_frames++;
                s_bytes += s_payload_len;
            } else {
                s_sum_err++;
            }
            s_state = ST_HUNT;
            break;
        }

        default:
            s_state = ST_HUNT;
            break;
        }
    }

    /* 超时重同步：200ms 收不到下一个字节，就回到"找帧头" */
    if (s_state != ST_HUNT && (millis() - s_last_byte_ms) > 200) {
        s_state = ST_HUNT;
        s_drops++;
    }
}

static bool image_rx_take(const uint8_t **data, uint32_t *len)
{
    if (!s_rdy_flag) return false;
    *data = s_rdy_buf;
    *len  = s_rdy_len;
    s_rdy_flag = false;      /* 一帧只画一次 */
    return true;
}

/* 专职收字节的任务：只跑状态机，绝不碰屏幕 */
static void imageRxTask(void *pvParam)
{
    (void)pvParam;
    for (;;) {
        image_rx_poll();
        /* 1ms 回来一次：就算 921600 也才积压 92 字节，远小于 8KB 缓冲 */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

#endif /* MODE_IMAGERX */

/* ============================================================
   显示任务（两种模式共用）
   ============================================================ */
static void displayTask(void * pvParam)
{
    (void)pvParam;

    tft_ui_init();                  /* 屏幕初始化（两种模式都要） */

#if (MODE == MODE_CLOCK)
    ui_create();                    /* 建时钟界面 */
    s_last_ms = millis();
    uint32_t last_clock = 0;
#endif

#if (MODE == MODE_IMAGERX)
    image_rx_init();                /* 串口 + 缓冲 + 提示文字 */
    uint32_t last_stat = 0;
#endif

    for (;;) {
#if (MODE == MODE_CLOCK)
        if (millis() - last_clock >= 200) {
            net_info_t net = {};
            if (s_netQueue && xQueueReceive(s_netQueue, &net, 0) == pdTRUE) {
                const int bars = rssi_to_bars(net.rssi, net.connected);
                for (int i = 0; i < 4; i++) {
                    lv_obj_set_style_bg_color(s_bar[i],
                        lv_color_hex(i < bars ? 0x35c1ff : 0x2a3542), 0);   /* 亮 / 暗 */
                }
            }
            last_clock = millis();
            clock_update();
        }

        tft_ui_handler();           /* LVGL 心跳（图传模式不跑，避免两套抢屏） */
        vTaskDelay(pdMS_TO_TICKS(5));
#endif

#if (MODE == MODE_IMAGERX)
        /* 收字节、组帧、校验已经交给 imageRxTask 专门做了，这里只管显示 */

        const uint8_t *jpg = NULL;
        uint32_t jpg_len = 0;
        if (image_rx_take(&jpg, &jpg_len)) {
#if (IMG_SHOW_ON_SCREEN)
            const uint32_t t0 = micros();
            if (tft_ui_draw_jpg(jpg, jpg_len, 0, 0)) s_drawn++;
            else                                     Serial.println("[IMG] jpeg decode FAIL");
            s_draw_us += micros() - t0;
            s_draw_n++;
#else
            s_drawn++;                  /* 只统计，不画屏 */
#endif
        }

        if (millis() - last_stat >= 1000) {     /* 每秒打一次统计 */
            last_stat = millis();
            static uint32_t last_total = 0;
            Serial.printf("[IMG] 帧=%u 显示=%u 丢弃=%u 校验错=%u 平均=%u 字节/帧  实收=%u 字节/s  画屏=%u us\r\n",
                          (unsigned)s_frames, (unsigned)s_drawn,
                          (unsigned)s_drops, (unsigned)s_sum_err,
                          (unsigned)(s_frames ? (s_bytes / s_frames) : 0),
                          (unsigned)(s_total_bytes - last_total),
                          (unsigned)(s_draw_n ? (s_draw_us / s_draw_n) : 0));
            last_total = s_total_bytes;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
#endif
    }
}

/* ============================================================
   启动
   ============================================================ */
void StartTask()
{
#if (MODE == MODE_CLOCK)
    /* 队列必须先于任务创建 */
    s_netQueue = xQueueCreate(1, sizeof(net_info_t));
#endif

    xTaskCreatePinnedToCore(displayTask, "display", STACK_DISPLAY, NULL, 2, &s_displayHandle, 1);

#if (MODE == MODE_CLOCK)
    xTaskCreatePinnedToCore(wifiTask, "wifi", STACK_WIFI, NULL, 1, &s_wifiHandle, 1);
#endif
}
