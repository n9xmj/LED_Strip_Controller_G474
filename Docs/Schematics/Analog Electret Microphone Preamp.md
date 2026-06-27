# Analog Electret Microphone Preamp for STM32 ADC
**Project:** Audio-Reactive Lighting Controller  
**Version:** 1.1 (LM358, 5 V powered, variable gain with 2N5457 VCR option)  
**Date:** May 2026

**Goal:** Amplify electret mic, center signal at 1.65 V (VrefADC/2), provide variable gain (manual pot or DAC-controlled), anti-alias filter, and protect STM32 3.3 V ADC input.

## BOM (Bill of Materials)
- IC1: LM358 (single/dual op-amp, DIP-8 preferred)
- MIC1: Standard 2-pin or 3-pin electret microphone capsule
- R1: 4.7 kOhm resistor (mic bias)
- R2, R3: 10 kOhm resistors (1.65 V divider)
- R4: 1 kOhm resistor (input Rin)
- R5: 1 kOhm resistor (series to ADC)
- R6: 10 kOhm resistor (JFET gate)
- R7: 47 kOhm - 100 kOhm resistor (optional min gain)
- C1: 1 uF - 10 uF electrolytic or ceramic (mic coupling)
- C2: 10 uF electrolytic or ceramic (Vmid bypass)
- C3: 22 nF ceramic (anti-alias filter)
- D1, D2: 1N4148 or BAT54 diodes (clamping)
- Q1: 2N5457 N-channel JFET (TO-92) - for DAC-controlled gain
- Power: +5 V & GND for LM358, 3.3 V for mic/STM32

All parts through-hole, breadboard / perfboard friendly.

## Step-by-Step Wiring Checklist (Heathkit Style)

**Power Connections**
- [ ] LM358 Pin 8 -> +5 V
- [ ] LM358 Pin 4 -> GND

**1.65 V Reference (Vmid)**
- [ ] 3.3 V -> R2 (10 kOhm) -> Vmid node
- [ ] Vmid node -> R3 (10 kOhm) -> GND
- [ ] Vmid node -> C2 (10 uF) -> GND (observe polarity)

**Mic Bias (2-pin version)**
- [ ] Mic - pin -> GND
- [ ] Mic + pin -> R1 (4.7 kOhm) -> 3.3 V

**Mic Bias (3-pin version - if you have one)**
- [ ] Mic GND pin -> GND
- [ ] Mic Vcc pin -> R1 (4.7 kOhm) -> 3.3 V
- [ ] Mic Signal pin -> C1 -> R4

**Mic Coupling & Input**
- [ ] Mic signal (after bias) -> C1 (1-10 uF) -> R4 (1 kOhm)
- [ ] Other end of R4 -> LM358 Pin 2 (- input)

**Non-Inverting Input**
- [ ] LM358 Pin 3 (+ input) -> Vmid node

**Output Filter & Protection**
- [ ] LM358 Pin 1 (output) -> R5 (1 kOhm) -> ADC_signal node
- [ ] ADC_signal node -> C3 (22 nF) -> GND
- [ ] ADC_signal node -> D1 anode; D1 cathode -> 3.3 V
- [ ] ADC_signal node -> D2 cathode; D2 anode -> GND

**Final Connection**
- [ ] ADC_signal node -> STM32 ADC pin (e.g. PA0)

**Unused Op-Amp (if dual LM358)**
- [ ] Tie unused inputs appropriately or configure as unity-gain buffer

## DAC-Controlled Gain Modification (2N5457 JFET VCR)

**Replace the manual pot with this VCR section:**

- [ ] LM358 Pin 1 (output) -> 2N5457 Drain (D)
- [ ] 2N5457 Source (S) -> LM358 Pin 2 (- input)   [or through R7 for min gain]
- [ ] 2N5457 Gate (G) -> R6 (10 kOhm) -> STM32 DAC output pin
- [ ] (Optional) R7 (47-100 kOhm) in series between Source and Pin 2
- [ ] (Optional) 100 nF capacitor from Gate to GND for smoothing

**2N5457 Pinout (TO-92, flat side facing you)**
- Left:   Drain (D)
- Middle: Gate (G)
- Right:  Source (S)

## DAC Drive Details (STM32 0-3.3 V DAC)

**Note:** The 2N5457 needs negative Vgs for full control range.

**Basic Connection (start here)**
- STM32 DAC pin -> R6 (10 kOhm) -> Gate
- Add a resistor from Gate to GND (22-47 kOhm) to scale voltage

**Recommended Simple Level Shifter (for better range)**
- DAC -> 22 kOhm -> Gate node
- Gate node -> 47 kOhm -> GND
- Add a small negative bias if needed (e.g. 100 kOhm from Gate to a -1.5 V point created with a divider from +5 V / GND)

**Expected Behavior**
- DAC approx 0 V     -> Highest gain (~50-80x)
- DAC approx 3.3 V   -> Lowest gain (~1-5x)

Test on breadboard and adjust resistors based on measured gain range with your specific 2N5457.

## Quick Test Procedure
1. Power up (5 V for LM358, 3.3 V for rest).
2. Check DC voltage at LM358 Pin 1 and ADC pin -> must be 1.65 V +-0.05 V (no sound).
3. Set DAC to mid-scale (~1.65 V) or start with low gain.
4. Speak/clap near mic and observe swing around 1.65 V.
5. Sweep DAC 0-3.3 V and measure gain change.
6. Verify signal stays safely between 0 V and 3.3 V.

**Expected Performance**
- DC bias locked at 1.65 V at any gain setting.
- Gain range: ~1x to ~80x (DAC controlled)
- Anti-alias cutoff: ~7-20 kHz (good for 40 kHz+ sampling).
- ADC protected by clamping diodes.