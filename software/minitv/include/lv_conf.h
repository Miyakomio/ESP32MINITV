/**
 * @file lv_conf.h
 * minitv 的 LVGL 9.6 配置（彩屏 / 16 位色）
 *
 * 目的：只编译用得到的部分，缩短编译时间。
 * 之前工程里没有 lv_conf.h，LVGL 用的是内部默认值——所有控件全开，
 * 一次要编 551 个 .c 文件。这份配置把用不到的关掉。
 *
 * 要用某个控件时把它改成 1 即可（改完需重新编译一次）。
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   颜色
 *====================*/
/* ST7789 走 RGB565，和 LovyanGFX 对齐。
   注意：9.6 里 LV_COLOR_DEPTH 已标记为弃用，要写 LV_COLOR_FORMAT_DEFAULT，
   颜色深度由它推导出来。 */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

/*====================
   内存
 *====================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
/* 界面复杂了往上调；小电视这种 64KB 够用 */
/* 写成 65536 是为了和 platformio.ini 里的 -DLV_MEM_SIZE=65536 完全一致，
   避免两边都生效时报 "macro redefined" */
#define LV_MEM_SIZE             65536

/*====================
   刷新节奏
 *====================*/
#define LV_DEF_REFR_PERIOD      16

/*====================
   字体（只留两个，省编译时间和 Flash）
 *====================*/
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_28   1   /* 时钟大字号 */
#define LV_FONT_MONTSERRAT_40   1   /* 大屏时钟用 */
#define LV_FONT_DEFAULT         LV_FONT_DEFAULT_MONTSERRAT_14

/*====================
   主题：只留默认主题
 *====================*/
#define LV_USE_THEME_DEFAULT    1
#define LV_USE_THEME_SIMPLE     0
#define LV_USE_THEME_MONO       0   /* 单色屏才需要 */

/*====================
   布局（保留，写界面很方便）
 *====================*/
#define LV_USE_FLEX             1
#define LV_USE_GRID             1

/*====================
   控件：只留小电视用得到的
 *====================*/
#define LV_USE_LABEL            1
#define LV_USE_IMAGE            1
#define LV_USE_BUTTON           1
#define LV_USE_BAR              1
#define LV_USE_SLIDER           1
#define LV_USE_ARC              1
#define LV_USE_LINE             1
#define LV_USE_LED              1
#define LV_USE_SPINNER          1
#define LV_USE_CHECKBOX         1
#define LV_USE_SWITCH           1
#define LV_USE_LIST             1
#define LV_USE_CANVAS           1   /* 自己画图用得上 */

/* ---- 下面这些暂时用不到，关掉省编译时间 ---- */
#define LV_USE_ANIMIMG          0
#define LV_USE_ARCLABEL         0
#define LV_USE_BUTTONMATRIX     0   /* 键盘的底层控件 */
#define LV_USE_CALENDAR         0
#define LV_USE_CHART            0   /* 要画曲线图就改成 1 */
#define LV_USE_DROPDOWN         0
#define LV_USE_IMAGEBUTTON      0
#define LV_USE_KEYBOARD         0
#define LV_USE_MENU             0
#define LV_USE_MSGBOX           0
#define LV_USE_ROLLER           0
#define LV_USE_SCALE            0
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_TABLE            0
#define LV_USE_TABVIEW          0
#define LV_USE_TEXTAREA         0
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0

/*====================
   第三方库 / 图片解码（小电视暂时不用）
 *====================*/
#define LV_USE_LODEPNG          0
#define LV_USE_LIBPNG           0
#define LV_USE_BMP              0
#define LV_USE_TJPGD            0   /* 想在 LVGL 里直接显示 JPG 就改成 1 */
#define LV_USE_GIF              0
#define LV_USE_QRCODE           0
#define LV_USE_BARCODE          0
#define LV_USE_FREETYPE         0
#define LV_USE_TINY_TTF         0
#define LV_USE_RLOTTIE          0
#define LV_USE_FFMPEG           0
#define LV_USE_THORVG           0   /* 矢量图形，用不到 */
#define LV_USE_LZ4              0

/*====================
   平台后端（ESP32 上用不到，全关）
 *====================*/
#define LV_USE_SDL              0
#define LV_USE_LINUX_FBDEV      0
#define LV_USE_LINUX_DRM        0
#define LV_USE_WAYLAND          0
#define LV_USE_X11              0
#define LV_USE_OPENGLES         0
#define LV_USE_EGL              0
#define LV_USE_GLFW             0
#define LV_USE_WINDOWS          0

/*====================
   文件系统（用 SD 卡再开）
 *====================*/
#define LV_USE_FS_STDIO         0
#define LV_USE_FS_POSIX         0
#define LV_USE_FS_WIN32         0
#define LV_USE_FS_FATFS         0

/*====================
   日志：调试时再开
 *====================*/
#define LV_USE_LOG              0

#endif /* LV_CONF_H */
