# TianShanOS GPIO 引脚映射表

> **目标板**: RM01_ESP32S3  
> **芯片**: ESP32-S3  
> **最后更新**: 2026-01-19  
> **验证状态**: ✅ 已与 PCB 图纸及 robOS 代码核对一致

---

## 引脚总览

| GPIO | 功能 | 方向 | 电平逻辑 | 初始状态 | 备注 |
|------|------|------|----------|----------|------|
| 0 | (保留) | - | - | - | Strapping pin |
| 1 | AGX_RESET | OUT | HIGH=复位 | LOW | 脉冲 1000ms |
| 2 | LPMU_RESET | OUT | HIGH=复位 | LOW | 脉冲 300ms |
| 3 | AGX_FORCE_SHUTDOWN | OUT | LOW=强制关机 | HIGH | Strapping pin |
| 4 | SD_D0 | IO | - | - | SDMMC |
| 5 | SD_D1 | IO | - | - | SDMMC |
| 6 | SD_D2 | IO | - | - | SDMMC |
| 7 | SD_D3 | IO | - | - | SDMMC |
| 8 | USB_MUX_0 | OUT | - | - | ESP32_MUX0_SEL |
| 9 | LED_MATRIX | OUT | WS2812 | - | 32x32 (1024颗) |
| 10 | ETH_CS | OUT | LOW=选中 | HIGH | W5500 SPI |
| 11 | ETH_MOSI | OUT | - | - | W5500 SPI |
| 12 | ETH_SCLK | OUT | - | - | W5500 SPI |
| 13 | ETH_MISO | IN | - | - | W5500 SPI |
| 14 | (空闲) | - | - | - | - |
| 15 | SD_CMD | IO | - | - | SDMMC |
| 16 | SD_CLK | OUT | - | - | 40MHz |
| 17 | RTL8367_RST | OUT | HIGH=复位 | LOW | 网络交换机 |
| 18 | POWER_ADC | IN | ADC2_CH7 | - | 分压 11.4:1 |
| 19-37 | (空闲/内部) | - | - | - | - |
| 38 | ETH_INT | IN | - | - | W5500 中断 |
| 39 | ETH_RST | OUT | LOW=复位 | HIGH | W5500 复位 |
| 40 | AGX_RECOVERY | OUT | HIGH=恢复模式 | LOW | - |
| 41 | FAN_PWM_0 | OUT | PWM | - | 25kHz, 唯一风扇 |
| 42 | LED_BOARD | OUT | WS2812 | - | 28颗状态灯 |
| 43 | UART0_TX | OUT | - | - | USB 串口 |
| 44 | UART0_RX | IN | - | - | USB 串口 |
| 45 | LED_TOUCH | OUT | WS2812 | - | 1颗, Strapping |
| 46 | LPMU_POWER | OUT | HIGH=电源键 | LOW | 脉冲 300ms, Strapping |
| 47 | POWER_UART_RX | IN | UART1_RX | - | 9600 8N1 |
| 48 | USB_MUX_1 | OUT | - | - | ESP32_MUX1_SEL |

---

## 功能分组

### 1. 设备控制 (AGX/LPMU)

| GPIO | 功能 | 电平逻辑 | 时序 |
|------|------|----------|------|
| 1 | AGX_RESET | HIGH=复位, LOW=正常 | 脉冲 1000ms |
| 2 | LPMU_RESET | HIGH=复位, LOW=正常 | 脉冲 300ms |
| 3 | AGX_FORCE_SHUTDOWN | LOW=强制关机, HIGH=正常 | 持续 LOW |
| 40 | AGX_RECOVERY | HIGH=恢复模式, LOW=正常 | 持续 HIGH |
| 46 | LPMU_POWER | HIGH=按下电源键 | 脉冲 300ms |

**操作示例**:
```c
// AGX 复位
gpio_set_level(1, 1);  // HIGH = reset
vTaskDelay(pdMS_TO_TICKS(1000));
gpio_set_level(1, 0);  // LOW = normal

// AGX 强制关机
gpio_set_level(3, 0);  // LOW = force off
vTaskDelay(pdMS_TO_TICKS(8000));
gpio_set_level(3, 1);  // HIGH = release
```

### 2. SD 卡 (SDMMC 4-bit)

| GPIO | 功能 | 说明 |
|------|------|------|
| 4 | SD_D0 | 数据线 0 |
| 5 | SD_D1 | 数据线 1 |
| 6 | SD_D2 | 数据线 2 |
| 7 | SD_D3 | 数据线 3 |
| 15 | SD_CMD | 命令线 |
| 16 | SD_CLK | 时钟 (40MHz) |

**配置** (`sdkconfig.defaults`):
```
CONFIG_TS_STORAGE_SD_CMD_GPIO=15
CONFIG_TS_STORAGE_SD_CLK_GPIO=16
CONFIG_TS_STORAGE_SD_D0_GPIO=4
CONFIG_TS_STORAGE_SD_D1_GPIO=5
CONFIG_TS_STORAGE_SD_D2_GPIO=6
CONFIG_TS_STORAGE_SD_D3_GPIO=7
```

### 3. USB MUX

| GPIO | 功能 | 说明 |
|------|------|------|
| 8 | USB_MUX_0 | ESP32_MUX0_SEL |
| 48 | USB_MUX_1 | ESP32_MUX1_SEL |

### 4. LED 系统 (WS2812)

| GPIO | 功能 | 数量 | 用途 |
|------|------|------|------|
| 9 | LED_MATRIX | 1024颗 (32x32) | 主显示屏 |
| 42 | LED_BOARD | 28颗 | 状态指示灯条 |
| 45 | LED_TOUCH | 1颗 | 触摸指示灯 |

**总计**: 1053 颗 WS2812 LED

### 5. W5500 以太网 (SPI2)

| GPIO | 功能 | 方向 | 说明 |
|------|------|------|------|
| 10 | ETH_CS | OUT | 片选 (LOW=选中) |
| 11 | ETH_MOSI | OUT | 主出从入 |
| 12 | ETH_SCLK | OUT | 时钟 |
| 13 | ETH_MISO | IN | 主入从出 |
| 38 | ETH_INT | IN | 中断信号 |
| 39 | ETH_RST | OUT | 复位 (LOW=复位, HIGH=正常) |

**SPI 配置**: SPI2_HOST, 25MHz

### 6. 网络交换机 (RTL8367)

| GPIO | 功能 | 电平逻辑 |
|------|------|----------|
| 17 | RTL8367_RST | HIGH=复位, LOW=正常 |

### 7. 风扇控制

| GPIO | 功能 | 参数 |
|------|------|------|
| 41 | FAN_PWM_0 | 25kHz, 10-bit 分辨率 |

**注意**: 板上只有一个风扇，无 FAN_PWM_1

### 8. 电源监控

| GPIO | 功能 | 协议/参数 |
|------|------|-----------|
| 18 | POWER_ADC | ADC2_CH7, 分压比 11.4:1, 最高 72V |
| 47 | POWER_UART_RX | UART1_RX, 9600 8N1 |

**电源芯片数据格式**:
```
[0xFF][电压][电流][CRC]  (4字节)
```

---

## Strapping Pins 注意事项

以下 GPIO 在启动时有特殊用途，需注意初始状态：

| GPIO | 功能 | 启动影响 | 使用建议 |
|------|------|----------|----------|
| 0 | Boot mode | 下拉=下载模式 | 避免使用 |
| 3 | AGX_FORCE_SHUTDOWN | - | 初始 HIGH |
| 45 | LED_TOUCH | VDD_SPI | 初始不驱动 |
| 46 | LPMU_POWER | Boot mode | 初始 LOW |

---

## 相关文件

| 文件 | 用途 |
|------|------|
| `boards/rm01_esp32s3/pins.json` | 引脚定义 (JSON) |
| `boards/rm01_esp32s3/devices.json` | 设备配置 |
| `components/ts_hal/include/ts_pin_manager.h` | 引脚函数枚举 |
| `components/ts_hal/src/ts_pin_manager.c` | 默认映射代码 |
| `sdkconfig.defaults` | SD 卡 GPIO 配置 |

---

## 变更历史

| 日期 | 变更内容 |
|------|----------|
| 2026-01-19 | 初始版本，与 PCB 图纸及 robOS 核对完成 |
| 2026-01-19 | 修正 SD 卡引脚 (4,5,6,7,15,16) |
| 2026-01-19 | 移除不存在的 FAN_PWM_1 (GPIO 40) |
| 2026-01-19 | 添加 RTL8367_RST (GPIO 17) |
| 2026-01-19 | 添加 POWER_ADC (GPIO 18) 和 POWER_UART_RX (GPIO 47) |
