# mk-clock-adult 2.1A BPI-M2 Zero R1 Bill of Materials

This is the reference hardware purchase list for the BPI-M2 Zero adult clock. The links below record the specific parts selected for the current build. Product listings can change, so match the part description and electrical/mechanical requirements rather than relying only on the seller title.

## Core electronics

| Part | Purchased item | Use in clock | Why this part was selected |
|:--|:--|:--|:--|
| Single-board computer | [52Pi Banana Pi BPI-M2 Zero](https://52pi.com/products/banana-pi-bpi-m2-zero-quad-core-allwinner-h3-512mb-ddr3-ram-support-linux-android-open-source-development-single-board-computer?variant=42515796066456) | Main controller | Compact 65 x 30 mm board with Allwinner H3, 512 MB RAM, onboard Wi-Fi/Bluetooth, and a 40-pin GPIO header exposing SPI, I2C and I2S. It provides the interfaces required by the OLED, AHT10 and MAX98357A without the cost or size of a larger SBC. This is the maintained adult-clock platform. |
| OLED display | [AliExpress item 1005008538880043](https://www.aliexpress.com/item/1005008538880043.html?spm=a2g0o.order_list.order_list_main.5.72631802mQy5Rt) | Main clock and Weather display | This is the purchased display source for the build. The software and enclosure are designed for the project's SSD1322 256 x 64 SPI OLED. The large monochrome panel gives high contrast, no backlight glow, and enough horizontal resolution for the clock plus three Weather panels. |
| I2S amplifier | [MAX98357 / MAX98357A I2S Class-D module, ASIN B0GXTLZC1P](https://www.amazon.ca/dp/B0GXTLZC1P?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) | Digital audio amplifier | The MAX98357A accepts I2S directly from the BPI, so no analog DAC or analog audio path is required. It is compact, efficient and appropriately sized for the 3 W speaker used in the clock. The current Device Tree overlay also controls amplifier shutdown to avoid startup/stop noise. |
| Speaker | [Dweii 4 ohm 3 W mini speaker, ASIN B0BTP67F81](https://www.amazon.ca/dp/B0BTP67F81?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) | Alarm and music output | 4 ohm / 3 W rating is a practical match for the MAX98357A module. The approximately 25 x 25 x 13 mm format is small enough for the enclosure while still providing materially better music and alarm output than a piezo buzzer. |
| External Wi-Fi antenna kit | [Gagool 8 dBi Wi-Fi antenna and U.FL/IPEX-to-RP-SMA pigtail, ASIN B09GTVZM23](https://www.amazon.ca/dp/B09GTVZM23?ref=ppx_yo2ov_dt_b_fed_asin_title) | Improves Wi-Fi reception | The BPI is installed inside a printed enclosure and reliable network connectivity is important for NTP, Weather and the web GUI. Moving the antenna outside the case reduces enclosure and placement losses and gives a better signal margin than relying on the board antenna alone. The kit includes the antenna and short pigtail needed to panel-mount the external connector. |

## Wiring and serviceability

| Part | Purchased item | Use in clock | Why this part was selected |
|:--|:--|:--|:--|
| General jumper wiring | [200-piece 10 cm breadboard jumper-wire kit, ASIN B08T1Y1YR8](https://www.amazon.ca/dp/B08T1Y1YR8?ref=ppx_yo2ov_dt_b_fed_asin_title) | Short internal signal wiring | Short pre-terminated jumpers simplify prototype and final internal wiring, keep excess wire out of the enclosure, and make GPIO changes or service easier than soldering every board connection permanently. |
| AHT10 sensor leads | [MECCANIXITY 2.54 mm female-to-female 20 cm jumper wires, ASIN B0F1YD5G5L](https://www.amazon.ca/dp/B0F1YD5G5L?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) | Connection to the inside temperature/humidity sensor | The separate 20 cm female-to-female leads give enough distance to locate the AHT10 away from the BPI and amplifier heat while retaining removable 2.54 mm header connections. Keeping the sensor away from heat-producing electronics improves the usefulness of the inside-temperature reading. |
| Speaker connector | [JST-PH 2.0 mm 2-pin connector sets with 28 AWG leads, ASIN B0F36N258J](https://www.amazon.ca/dp/B0F36N258J?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) | Preferred removable speaker connection | A 2-pin polarized connector is preferred between the amplifier and speaker. Solder the mating connection to the amplifier speaker output rather than permanently hard-wiring the speaker. This makes enclosure assembly, speaker replacement and service much easier while preserving SPK+ / SPK- polarity. |

## Power cable

| Part | Purchased item | Use in clock | Why this part was selected |
|:--|:--|:--|:--|
| External power cable | [SMALLElectric 6 ft USB-A to USB-C braided cable, 5-pack, ASIN B082Z1YYRK](https://www.amazon.ca/dp/B082Z1YYRK?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) | External 5 V power/charging lead | The 6 ft braided cable provides an inexpensive, replaceable power lead with useful reach and is rated by the listing for up to 3 A charging. **The BPI-M2 Zero itself is not powered through USB-C.** In this project, regulated +5 V ultimately feeds CON2 physical pin 4 and GND feeds physical pin 6; the cable is only part of the external power path. |

## Mechanical parts

| Part | Purchased item | Use in clock | Why this part was selected |
|:--|:--|:--|:--|
| Case filament | [Polymaker PolyLite ASA 1.75 mm white 1 kg, ASIN B09DKN3S9J](https://www.amazon.ca/dp/B09DKN3S9J?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) | 3D-printed enclosure | ASA was selected over PLA for better heat resistance, toughness, dimensional stability in service, and UV/weather resistance. The clock contains an SBC, amplifier and display, so ASA provides more temperature margin than PLA for a long-lived enclosure. The production enclosure dimensions are tuned for ASA shrinkage. |
| Screw assortment | [Kalote 780-piece M2 metric screw kit, ASIN B0D7QCS5FL](https://www.amazon.ca/dp/B0D7QCS5FL?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) | Internal mounting and assembly | The assortment provides M2 screws in several useful lengths plus matching hardware. Using machine screws instead of adhesive makes the OLED, boards and internal parts replaceable and allows the printed mounts to be serviced repeatedly. |
| Rubber feet | [FURNIMATE black self-adhesive rubber feet, 16-piece large trapezoid set, ASIN B085DMB1XV](https://www.amazon.ca/dp/B085DMB1XV?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) | Non-slip enclosure feet | The approximately 0.75 x 0.3 inch self-adhesive bumpers add grip, isolate the printed case from the desk surface, reduce vibration transfer from the speaker, and avoid printing separate flexible feet. Their adhesive backing also makes replacement simple. |

## Required items not represented by the purchase links above

The clock design also requires the following items. They are part of the build even though a specific purchase link was not supplied in this BOM:

- **AHT10 temperature/humidity sensor module** - I2C address `0x38`; mounted away from major heat sources.
- **TTP223B capacitive touch sensor** - provides the physical touch input used by the clock.
- **Regulated 5 V power supply** - feeds +5 V directly to BPI CON2 physical pin 4 and GND to physical pin 6. Use one board-power source only.
- **microSD card** - contains the validated Armbian image and clock software. Use a card proven to boot reliably on the BPI-M2 Zero. The Gigastone MLC card previously tested with this project did not boot reliably, while a basic compatible microSD card did.

## Component pairing rationale

The audio chain is intentionally simple:

```text
BPI-M2 Zero I2S -> MAX98357A -> 4 ohm / 3 W speaker
```

This keeps audio digital until the Class-D amplifier and avoids a separate USB or analog sound device.

The display and environmental sensor use separate buses:

```text
SSD1322 OLED -> SPI0
AHT10        -> I2C0
```

This lets the OLED update quickly without interfering with the low-rate AHT10 measurements.

The external antenna is used because the clock is a continuously connected appliance. NTP, ECCC Weather retrieval and the web interface all benefit from additional Wi-Fi signal margin, especially once the BPI is enclosed.

## Reference software platform

The hardware above is validated with:

```text
Armbian_community_26.8.0-trunk.413_Bananapim2zero_trixie_current_6.18.38_minimal
```

See `install.md` for image preparation, first-boot Wi-Fi provisioning and installation. See `pinouts.md` for the authoritative wiring map.
