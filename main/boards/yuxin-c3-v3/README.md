# Yuxin C3 V3

基于 Xmini C3 V3 的开发板，支持语音交互、温湿度传感器和 OLED 显示。

## 硬件特性

### 芯片
- ESP32-C3 双核 RISC-V 处理器
- 内置 WiFi 和蓝牙

### 音频
- ES8311 音频编解码器
- I2S 数字音频接口
- 麦克风输入 / 扬声器输出

### 显示
- SSD1306 OLED 显示屏 (128x64)
- I2C 接口

### 传感器
- AHT20 温湿度传感器 (I2C, 地址 0x38)

### 电源管理
- ADC 电池电压监测
- 充电状态检测

## 引脚定义

| 引脚 | 功能 |
|------|------|
| GPIO 0 | I2C SDA (音频编解码器/AHT20) |
| GPIO 1 | I2C SCL (音频编解码器/AHT20) |
| GPIO 2 | 内置 LED |
| GPIO 4 | I2S DOUT (扬声器) |
| GPIO 5 | I2S WS |
| GPIO 6 | I2S DIN (麦克风) |
| GPIO 7 | I2S BCLK |
| GPIO 8 | I2S MCLK |
| GPIO 9 | BOOT 按钮 |
| GPIO 10 | 音频 PA 使能 |
| GPIO 12 | 充电状态检测 |
| GPIO 13 | ADC 电池电压输入 |

## 软件功能

### 语音交互
- 语音唤醒和对话
- 支持按键说话模式 (Press-to-Talk)

### 温湿度监测
- 语音查询当前温湿度
- 屏幕显示温湿度数据
- MCP 工具接口：`self.sensor.get_temperature_humidity`

### 电源管理
- 低功耗定时器
- 自动进入/退出休眠模式
- 充电状态实时监测

### 屏幕显示
- OLED 128x64 分辨率
- 支持通知显示
- 支持主题切换

## MCP 工具

### 传感器工具
| 工具名称 | 描述 |
|---------|------|
| `self.sensor.get_temperature_humidity` | 获取当前温湿度数据 |
| `self.set_press_to_talk` | 切换按键说话模式 |
| `self.get_device_status` | 获取设备状态（含温湿度） |

## 使用方法

### 编译配置
在 ESP-IDF 配置界面中选择：
```
Board Type → Yuxin C3 V3
```

### 语音查询温湿度
用户可以通过语音命令查询：
- "现在温度是多少？"
- "湿度多少？"
- "帮我查一下当前的温湿度"

### 硬件连接
确保 AHT20 传感器连接到 I2C 总线：
- SDA → GPIO 0
- SCL → GPIO 1
- VCC → 3.3V
- GND → GND

## 文件结构

```
yuxin-c3-v3/
├── config.h          # 硬件配置定义
├── config.json       # 构建配置
├── aht20.h           # AHT20 传感器驱动头文件
├── aht20.cc          # AHT20 传感器驱动实现
├── music_player.h    # 音乐播放器头文件
├── music_player.cc   # 音乐播放器实现
├── yuxin_c3_board.cc # 板级实现代码
└── README.md         # 项目说明文档
```