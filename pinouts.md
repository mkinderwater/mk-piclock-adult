# mk-clock-adult 2.1A BPI-M2 Zero R1 Pinouts

The application uses `/dev/gpiochip0` Allwinner line offsets rather than BCM numbering. Physical pins refer to the Banana Pi M2 Zero CON2 header.

Every pin below has been verified against the official Banana Pi BPI-M2 Zero 40-pin GPIO reference, against the mk-piclock v1.9.17 BPI-M2 Zero R18 pinout reference, and against this release's own `hardware_profile.h` and `hardware/max98357a-bpi-m2-zero.dts`. `gpiochip0` line numbers follow the standard Allwinner sunxi scheme (`(port index) * 32 + pin`, with A=0, B=1, C=2), which both references and this file already use consistently for every port-A pin (for example PA19 is line 19).

This release has no RGB LED hardware or driver code, so unlike the sibling mk-piclock kid-clock release, physical pins 29, 31, and 33 are unused here.

## Complete wiring

| Device | Signal | BPI signal | Line | Physical pin |
|:--|:--|:--|--:|--:|
| Board power | USB-C VBUS (+) | 5 V | - | USB-C connector, pin A4/A9/B4/B9 |
| Board power | USB-C GND (-) | Ground | - | USB-C connector, pin A1/A12/B1/B12 |
| SSD1322 | VCC_IN | 3.3 V | - | 1 |
| SSD1322 | VSS | Ground | - | 6 |
| SSD1322 | D1 / DIN | PC0 / SPI0 MOSI | 64, SPI-owned | 19 |
| SSD1322 | D0 / CLK | PC2 / SPI0 SCLK | 66, SPI-owned | 23 |
| SSD1322 | CS# | PC3 / SPI0 CS0 | 67, SPI-owned | 24 |
| SSD1322 | D/C# | PA2 | 2 | 22 |
| SSD1322 | RES# | PA0 | 0 | 13 |
| AHT10 | VCC | 3.3 V | - | 17 |
| AHT10 | GND | Ground | - | 9 |
| AHT10 | SDA | PA12 / TWI0 SDA | 12, I2C-owned | 3 |
| AHT10 | SCL | PA11 / TWI0 SCK | 11, I2C-owned | 5 |
| MAX98357A | VIN | 5 V | - | 2 |
| MAX98357A | GND | Ground | - | 14 |
| MAX98357A | BCLK | PA19 / I2S0 BCLK | I2S-owned | 27 |
| MAX98357A | LRC | PA18 / I2S0 LRCLK | I2S-owned | 28 |
| MAX98357A | DIN | PA20 / I2S0 DOUT | I2S-owned | 40 |
| MAX98357A | SD / EN | PA1 | codec-owned | 11 |
| TTP223B | VCC | 3.3 V | - | 17 |
| TTP223B | GND | Ground | - | 39 |
| TTP223B | OUT | PA21 | 21 | 38 |

## Header map

```text
                              BPI-M2 Zero CON2

OLED VCC       <- 3.3 V       (1)  (2)  5 V          -> MAX98357A VIN
AHT10 SDA      <- PA12        (3)  (4)  5 V
AHT10 SCL      <- PA11        (5)  (6)  GND          -> OLED GND
                              (7)  (8)
AHT10 GND      <- GND         (9) (10)
MAX98357A EN   <- PA1        (11) (12)
OLED RST       <- PA0        (13) (14) GND           -> MAX98357A GND
                             (15) (16)
AHT10/TTP VCC  <- 3.3 V      (17) (18)
OLED MOSI      <- PC0        (19) (20) GND
OLED MISO unused, PC1        (21) (22) PA2           -> OLED DC
OLED SCLK      <- PC2        (23) (24) PC3           -> OLED CS
                             (25) (26)
MAX98357A BCLK <- PA19       (27) (28) PA18          -> MAX98357A LRC
                             (29) (30)
                             (31) (32)
                             (33) (34)
                             (35) (36)
                             (37) (38) PA21          -> TTP223B OUT
TTP223B GND    <- GND        (39) (40) PA20          -> MAX98357A DIN
```

The board itself is powered separately through its onboard USB-C connector, not through the GPIO header: USB-C pins A4/A9/B4/B9 carry VBUS (+) and pins A1/A12/B1/B12 carry GND (-). The 5 V and GND pins on CON2 above are outputs from that supply used to power the peripherals, not a board power input.

The AHT10 uses `/dev/i2c-0` at address `0x38`. The OLED uses `/dev/spidev0.0`. The speaker output is differential and must connect only between `SPK+` and `SPK-`.

The custom audio overlay sets a 256x master-clock ratio and controls amplifier shutdown through PA1. The core forces mono MP3 files into two-channel PCM so the H2+/H3 I2S controller produces a standard two-slot frame.

### No speaker pop on this platform

This BPI wiring has not shown a MAX98357A startup/stop click. The overlay's `sdmode-gpios` and `sdmode-delay = <5>` let the MAX98357A codec driver itself hold PA1 low while idle, sequence it high only after the I2S clocks are running, and drop it again before the clocks stop. The amplifier is therefore never listening during a clock start/stop transition.
