# Tympanometer

A low-cost tympanometer designed to diagnose hearing loss in children, developed as part of the [Duke BME Design Fellows](https://bme.duke.edu/academics/undergrad/design-fellows/) program at Duke University's Pratt School of Engineering under the supervision of [Dr. Mark Palmeri, MD PhD](https://github.com/mlp6).

- [Duke BME Designers Develop a New Tool to Inexpensively Diagnose Hearing Loss in Children](https://pratt.duke.edu/news/duke-bme-designers-develop-a-new-tool-to-inexpensively-diagnose-hearing-loss-in-children/)
- [Preventing Childhood Hearing Loss Worldwide](https://pratt.duke.edu/news/preventing-childhood-hearing-loss-worldwide/)

---

## Team

**Iris Ye** · **Alisha Gupta** · **James Mu**

![Team](assets/team.jpg)


---

## Repository Structure

This repo has three active branches, one per contributor:

| Branch | Owner | Focus |
|--------|-------|-------|
| `main` | James Mu | Firmware, OLED display integration, microphone PCB |
| *(Alisha's branch)* | Alisha Gupta | Audio/microphone analog circuit PCB |
| *(Iris's branch)* | Iris Ye | Isolated microphone PCB and analog filtering |

---

## What We've Built

### Firmware & OLED Display — James Mu

Integrated a GME12864-13 (128×64 SSD1306) OLED display into the device running on the nRF52833 DK under Zephyr RTOS. The display renders a real-time tympanogram trace with a split-zone layout:

- **Yellow zone (rows 0–15):** ear canal volume (ECV), static admittance (SA), and peak pressure (PPP)
- **Blue zone (rows 16–63):** tympanogram curve plotted from −400 to +200 daPa

The firmware supports all five standard tympanogram curve types (A, As, Ad, B, C) and currently uses physiologically accurate simulated data. Real ADC input will replace the simulation when the analog front-end is complete.

A custom 3×5 bitmap font is used to minimize flash footprint.

**Hardware:**
- MCU: nRF52833 DK
- Display: GME12864-13 OLED (SSD1306, I2C at 0x3C)
- I2C pins: SDA → P0.27, SCL → P0.26

### Analog Circuitry — Alisha Gupta & Iris Ye

First-iteration PCB designs for the microphone and audio signal chain, focused on filtering out 60 Hz noise from the microphone signal. The team has been exploring different analog filter architectures (including a notch filter redesign) to isolate the tympanometry probe tone from interference.

Key components include:
- **LM4875MM** audio amplifier (SOP-8)
- **TLV313IDBVT** op-amps (×2, TSOT-23-5) for active filtering
- 0603 SMD passive components throughout

A full bill of materials is in [`audio_and_microphone/BOM.csv`](audio_and_microphone/BOM.csv).

---

## Build & Flash (Firmware)

```bash
west build -b nrf52833dk/nrf52833
west flash
```

Requires a [Zephyr RTOS](https://docs.zephyrproject.org/) environment set up with the nRF Connect SDK.

---

*This README was drafted with the assistance of Claude (Anthropic) based on information provided by the team.*
