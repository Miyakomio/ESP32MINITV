# ESP32 MINI TV

基于 ESP32-S3 的迷你电视与智能手表项目，包含软件源码和硬件设计资料。

## 目录

- `software/minitv`：PlatformIO 工程，使用 ESP32-S3、LVGL、LovyanGFX 和 TinyPMU。
- `hardware/智能手表`：ESP32-S3 多功能手表 PCB 工程、原理图 PDF 和 PCB 预览图。

## 构建

软件工程使用 PlatformIO。首次编译前，请在
`software/minitv/lib/bsp/WIFI/wifilink.h` 中填写自己的 WiFi 名称和密码。

```text
pio run
```

## 说明

仓库不包含 PlatformIO 依赖、构建缓存、固件二进制文件或本机 IDE 配置。
