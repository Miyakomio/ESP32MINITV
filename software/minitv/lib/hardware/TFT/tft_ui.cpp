#include "tft_ui.h"

#include "esp_heap_caps.h"

/* LovyanGFX 必须先用 LGFX_USE_V1 打开新版接口，
   否则它会走旧的 v0 接口（没有 lgfx:: 命名空间），下面全部报错 */
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

/* ============================================================
   1. 屏幕底层（LovyanGFX）：SPI + 面板型号 + 背光
   ============================================================ */
class LGFX_MINITV : public lgfx::LGFX_Device
{
#if TFT_IS_ST7735
    lgfx::Panel_ST7735S _panel;
#else
    lgfx::Panel_ILI9341 _panel;
#endif
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;

public:
    LGFX_MINITV()
    {
        /* --- SPI 总线 --- */
        {
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;                              /* 花屏就试 3 */
            cfg.freq_write  = TFT_SPI_WRITE_HZ;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = true;                           /* 只写，不用 MISO */
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = TFT_SCLK_PIN;
            cfg.pin_mosi    = TFT_MOSI_PIN;
            cfg.pin_miso    = -1;
            cfg.pin_dc      = TFT_DC_PIN;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        /* --- 面板参数 --- */
        {
            auto cfg = _panel.config();
            cfg.pin_cs           = TFT_CS_PIN;                /* OpenMV 模块 CS 是独立脚 */
            cfg.pin_rst          = TFT_RST_PIN;
            cfg.pin_busy         = -1;
            cfg.panel_width      = TFT_WIDTH;
            cfg.panel_height     = TFT_HEIGHT;
            cfg.offset_x         = TFT_OFFSET_X;
            cfg.offset_y         = TFT_OFFSET_Y;
            cfg.offset_rotation  = TFT_OFFSET_ROTATION;
            cfg.readable         = true;
            cfg.invert           = TFT_INVERT ? true : false;
            cfg.rgb_order        = TFT_RGB_ORDER ? true : false;
            cfg.bus_shared       = false;
            _panel.config(cfg);
        }
        /* --- 背光 --- */
        {
            auto cfg = _light.config();
            cfg.pin_bl      = TFT_BL_PIN;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }

    /* 运行期改面板偏移（校准用）：改完要重新 init() 才生效 */
    void setPanelOffset(int x, int y)
    {
        auto cfg = _panel.config();
        cfg.offset_x = x;
        cfg.offset_y = y;
        _panel.config(cfg);
    }

    /* 运行期改面板分辨率（校准用）：改完要重新 init() 才生效 */
    void setPanelSize(int w, int h)
    {
        auto cfg = _panel.config();
        cfg.panel_width  = w;
        cfg.panel_height = h;
        _panel.config(cfg);
    }
};

static LGFX_MINITV    lcd;
static lv_display_t * s_disp = NULL;

/* LVGL 9 需要一个毫秒级时基回调（否则定时器/刷新都不走） */
static uint32_t lv_tick_cb(void)
{
    return (uint32_t)millis();
}

/* ============================================================
   2. LVGL 画布缓冲（整屏两个，放 PSRAM）
   ============================================================ */
#define DISP_BYTES_PER_PX   LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565)
#define DISP_BUF_BYTES      ((size_t)TFT_WIDTH * TFT_HEIGHT * DISP_BYTES_PER_PX)

static uint8_t * s_buf1 = NULL;
static uint8_t * s_buf2 = NULL;
/* PSRAM 申请失败时的退路；必须对齐，LVGL 的绘制缓冲有对齐要求 */
static uint8_t   s_small_buf[TFT_WIDTH * (TFT_HEIGHT / 10) * 2] __attribute__((aligned(64)));

/* ============================================================
   3. 把 LVGL 画好的像素刷到屏幕
   ============================================================ */
static void disp_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    const uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    const uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    lcd.startWrite();
    lcd.setAddrWindow(area->x1, area->y1, w, h);
    lcd.pushColors((uint16_t *)px_map, w * h, true);
    lcd.endWrite();

    lv_display_flush_ready(disp);
}

/* ============================================================
   4. 对外接口
   ============================================================ */
void tft_ui_init(void)
{
    /* 先把屏幕点亮 */
    lcd.init();
    lcd.setRotation(TFT_ROTATION);
    lcd.setBrightness(200);

#if TFT_DIAG_MODE
    /* ------------------------------------------------------------
       屏幕标定模式：循环试【分辨率 + 偏移】组合，每组 3 秒
       每组画面：一圈白框（四条边都要贴住屏幕边缘）
                 + 屏幕正中央一个绿色正圆（看到椭圆 = 分辨率不对）
                 + 左上角编号 #n / 分辨率 / 偏移值
       找到"白框四边贴边 + 绿圆是正圆"的那组，把编号告诉我
       ------------------------------------------------------------ */
    static const int cand[][4] = {   /* {宽, 高, offset_x, offset_y} */
        {128,160, 0,0}, {128,160, 2,1}, {128,160, 1,2},
        {128,160, 0,1}, {128,160, 1,0}, {128,160, 2,0},
        {128,160, 0,2}, {128,160, 1,1}, {128,160, 2,2},
        {160,128, 0,0}, {160,128, 2,1}, {160,128, 1,2},
        {160,128, 0,1}, {160,128, 1,0}, {160,128, 2,0},
        {160,128, 0,2}, {160,128, 1,1}, {160,128, 2,2},
    };
    const int cand_n = (int)(sizeof(cand) / sizeof(cand[0]));

    Serial.println("[TFT] ===== 屏幕标定模式：18 组组合，每组 3 秒，循环播放 =====");

    for (int i = 0; ; i = (i + 1) % cand_n) {
        lcd.setPanelSize(cand[i][0], cand[i][1]);
        lcd.setPanelOffset(cand[i][2], cand[i][3]);
        lcd.init();
        lcd.setRotation(TFT_ROTATION);
        lcd.setBrightness(255);
        lcd.fillScreen(TFT_BLACK);

        /* 白框：四条边都要贴住屏幕边缘 */
        lcd.drawRect(0, 0, cand[i][0], cand[i][1], TFT_WHITE);
        /* 正圆：如果画出来是椭圆，说明"分辨率"这一项不对 */
        const int r = (cand[i][0] < cand[i][1] ? cand[i][0] : cand[i][1]) / 3;
        lcd.drawCircle(cand[i][0] / 2, cand[i][1] / 2, r, TFT_GREEN);

        lcd.setTextColor(TFT_WHITE, TFT_BLACK);
        lcd.setTextSize(1);
        lcd.setCursor(4, 4);
        lcd.printf("#%d", i);
        lcd.setCursor(4, 16);
        lcd.printf("%dx%d", cand[i][0], cand[i][1]);
        lcd.setCursor(4, 28);
        lcd.printf("X%d Y%d", cand[i][2], cand[i][3]);

        Serial.printf("[TFT] 第 %d 组: %dx%d  offset(%d,%d)\r\n",
                      i, cand[i][0], cand[i][1], cand[i][2], cand[i][3]);
        delay(3000);
    }
#endif

    /* ------------------------------------------------------------
       面板自检：依次全屏刷 红 → 绿 → 蓝 → 黑（各 500ms）
         · 三色能正常显示  → 面板型号 / 接线 / SPI 参数都对，问题在 LVGL 那边
         · 黑白乱闪 / 颜色不对 → 面板型号、SPI 参数或接线有问题
       自检完会自动进入时钟界面，不用管它。
       ------------------------------------------------------------ */
    lcd.fillScreen(TFT_RED);   delay(500);
    lcd.fillScreen(TFT_GREEN); delay(500);
    lcd.fillScreen(TFT_BLUE);  delay(500);
    lcd.fillScreen(TFT_BLACK);
    Serial.println("[TFT] panel self-test done: 应该依次闪过 红/绿/蓝");

    Serial.printf("[TFT] lcd init done (%dx%d)\r\n", (int)lcd.width(), (int)lcd.height());

    /* ------------------------------------------------------------
       ★ 关键：LVGL 必须先 lv_init()，再设置时基！
       少了这两步，后面的 lv_display_create() 会在未初始化状态下崩掉，
       现象就是"面板自检颜色正常，但界面不显示 / 一直重启"。
       ------------------------------------------------------------ */
    lv_init();
    lv_tick_set_cb(lv_tick_cb);
    Serial.println("[TFT] lvgl init done");

    /* 建 LVGL 显示对象 */
    s_disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    /* 整屏双缓冲 */
    s_buf1 = (uint8_t *)heap_caps_malloc(DISP_BUF_BYTES, MALLOC_CAP_SPIRAM);
    s_buf2 = (uint8_t *)heap_caps_malloc(DISP_BUF_BYTES, MALLOC_CAP_SPIRAM);

    if (s_buf1 && s_buf2) {
        Serial.printf("[TFT] PSRAM double buffer %u bytes x2, free PSRAM %u\r\n",
                      (unsigned)DISP_BUF_BYTES, (unsigned)ESP.getFreePsram());
        lv_display_set_buffers(s_disp, s_buf1, s_buf2, DISP_BUF_BYTES, LV_DISPLAY_RENDER_MODE_FULL);
    } else {
        Serial.println("[TFT] PSRAM not enough, use 1/10 screen buffer");
        if (s_buf1) { heap_caps_free(s_buf1); s_buf1 = NULL; }
        if (s_buf2) { heap_caps_free(s_buf2); s_buf2 = NULL; }
        lv_display_set_buffers(s_disp, s_small_buf, NULL, sizeof(s_small_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    }

    lv_display_set_flush_cb(s_disp, disp_flush_cb);
}

void tft_ui_handler(void)
{
    lv_timer_handler();      /* LVGL 的心跳，必须周期调用 */
}

void tft_ui_set_brightness(uint8_t v)
{
    lcd.setBrightness(v);
}

bool tft_ui_draw_jpg(const uint8_t *jpg, uint32_t len, int x, int y)
{
    if (jpg == NULL || len == 0) return false;
    return lcd.drawJpg(jpg, len, x, y) != 0;   /* LovyanGFX 内置 JPEG 解码 */
}

void tft_ui_show_msg(const char *msg)
{
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextSize(2);
    lcd.setCursor(6, (int)lcd.height() / 2 - 10);
    lcd.print(msg);
    lcd.setTextSize(1);
}


