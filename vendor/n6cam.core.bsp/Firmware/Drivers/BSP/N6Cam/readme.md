# N6Cam.BSP

## Board Definitions

| Preprocessor      | Description                      |
|-------------------|----------------------------------|
| N6CAM             | N6Cam support                    |
| N6CAM_WIFI_MURATA | Murata Wifi support              |
| STM32N6DK_C02     | DK-C02 support (limited: UVC/NN) |

## Clock Configuration
### Sources
Consider the following clock sources:

| SRC | FREQ      |
|-----|-----------|
| HSE | 48MHz     |
| HSI | 64MHz     |
| LSE | 32.768KHz |

### PLLs
Configured as:

| PLL  | SRC | M | N   | P1 | P2 | FREQ    |
|------|-----|---|-----|----|----|---------|
| PLL1 | HSI | 1 | 25  | 1  | 1  | 1600MHz |
| PLL2 | HSI | 8 | 125 | 1  | 1  | 1000MHz |
| PLL3 | HSI | 8 | 225 | 2  | 1  | 900MHz  |
| PLL4 | OFF | - | -   | -  | -  | -       |

**Note:**
- PLL4 is being configured by the audio peripheral

### System
System clocks are assigned using IC1, IC2, IC6 and IC11 configuration:

|       | IC   | SRC  | DIV | FREQ    |
|-------|------|------|-----|---------|
| SYSA  | IC1  | PLL1 | 2   | 800MHz  |
| SYSB  | IC2  | PLL1 | 4   | 400MHz  |
| SYSC  | IC6  | PLL2 | 1   | 1000MHz |
| SYSD  | IC11 | PLL3 | 1   | 900MHz  |


### Peripherals
Configured as:

| PHY    | IC    | SRC  | DIV | FREQ       |
|--------|-------|------|-----|------------|
| CSI    | IC18  | PLL1 | 80  | 20MHz      |
| DCMIPP | IC17  | PLL2 | 3   | 333.333MHz |
| IC1    | PCLK1 | -    | -   | 200MHz     |
| IC2    | PCLK1 | -    | -   | 200MHz     |
| IC3    | PCLK1 | -    | -   | 200MHz     |
| RTC    | LSE   | -    | -   | 32.768KHz  |
| SDMMC2 | HCLK  | -    | -   | 200MHz     |
| SPI3   | IC9   | PLL1 | 20  | 80MHz      |
| SPI4   | IC9   | PLL1 | 20  | 80MHz      |
| SPI5   | IC9   | PLL1 | 20  | 80MHz      |
| USART1 | IC14  | PLL1 | 16  | 100MHz     |
| USART2 | IC14  | PLL1 | 16  | 100MHz     |
| USB1   | HSE   | -    | -   | 48MHz      |
| XSPI1  | HCLK  | -    | -   | 200MHz     |
| XSPI2  | HCLK  | -    | -   | 200MHz     |

## DMA Usage
Currently assigned:

| PHY   | DMA    | RX   | TX   |
|-------|--------|------|------|
| UART1 | GPDMA1 | CH0  | CH1  |
| UART2 | GPDMA1 | CH2  | CH3  |
| I2C1  | GPDMA1 | CH4  | CH5  |
| I2C3  | GPDMA1 | CH6  | CH7  |
| I2C4  | GPDMA1 | CH8  | CH9  |
| SPI3  | GPDMA1 | CH10 | CH11 |
| SPI4  | GPDMA1 | CH13 | CH12 |
| SPI5  | GPDMA1 | CH14 | CH15 |
