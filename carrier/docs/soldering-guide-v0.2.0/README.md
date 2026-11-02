# Carrier v0.2.0 — Soldering Guide

v0.2.0 board (80.1 × 58.8 mm), 2026-11-01. Interactive version: this folder's `index.html` (on the site: `soldering-guide-v0.2.0/`).

Solder the 40 parts in number order, with a multimeter check of the power supply before the modules go on. The back goes first, while both sides are still flat: the small 0805 parts (red), then the chips and diodes (blue). Then the front: the low SMD parts (green), the through-hole parts (purple), the power check (gray), the two modules (amber), and the Hall sensors last, back on the back (teal). The back picture is drawn as you see it when soldering the back, so it is mirrored left-right; directions for back parts refer to that picture.

![Front, numbered](v0.2.0_front.png)

![Back, numbered (mirrored)](v0.2.0_back.png)

## Parts in soldering order

| # | Side | Part | LCSC | Orientation |
| --- | --- | --- | --- | --- |
| 1 | back | 47k resistor (R1) | C17713 | None |
| 2 | back | 10k resistor (R2) | C17414 | None |
| 3 | back | 10k resistor (R11) | C17414 | None |
| 4 | back | 10k resistor (R13) | C17414 | None |
| 5 | back | 10k resistor (R16) | C17414 | None |
| 6 | back | 1k resistor (R3) | C17513 | None |
| 7 | back | 1k resistor (R14) | C17513 | None |
| 8 | back | 100 Ω resistor (R4) | C17408 | None |
| 9 | back | 3.3k resistor (R15) | C26010 | None |
| 10 | back | 4.7k resistor (R17) | C17673 | None |
| 11 | back | 4.7k resistor (R18) | C17673 | None |
| 12 | back | 100 nF capacitor (C5) | C49678 | None |
| 13 | back | 100 nF capacitor (C6) | C49678 | None |
| 14 | back | 100 nF capacitor (C7) | C49678 | None |
| 15 | back | 100 nF capacitor (C13) | C49678 | None |
| 16 | back | 4.7 µF capacitor (C8) | C1779 | None |
| 17 | back | 2N7002 transistor (SOT-23) (Q2) | C8545 | Single leg on the right |
| 18 | back | 15 V zener (SOD-123) (D2) | C2126 | Band (−) down |
| 19 | back | XL1509 buck regulator (SOIC-8) (U2) | C74192 | Pin 1 dot top-left |
| 20 | back | DS3231 clock (SOIC-16) (U5) | C9866 | Pin 1 dot top-left |
| 21 | back | 10 µF 50 V capacitor (1210) (C14) | C138687 | None |
| 22 | back | SS14 diode (5 V path) (D4) | C2480 | Band (−) to the left |
| 23 | back | SS16 diode (buck) (D5) | C2481 | Band (−) to the left |
| 24 | back | SS14 diode (relay coil) (D6) | C2480 | Band (−) down |
| 25 | back | 24 V TVS (SMB) (D1) | C83268 | Either way round (bidirectional) |
| 26 | back | P-channel MOSFET (DPAK) (Q1) | C513222 | Big tab up, two legs down |
| 27 | front | 3 A fuse (F1) | C178991 | None |
| 28 | front | 68 µH inductor (L1) | C168091 | None |
| 29 | front | 220 µF 10 V capacitor (C16) | C7469982 | + pad on the left, black stripe (−) right |
| 30 | front | 100 µF 50 V capacitor (C4) | C178548 | + pad on the left, black stripe (−) right |
| 31 | front | IR receiver (U6) | C141632 | Stand it with the lens toward the top of the board, then down into the square. OUT leg on the right |
| 32 | front | Motor connector (right angle) (J2) | C157925 | Opening toward the left edge |
| 33 | front | CR2032 holder (BT1) | C5239862 | + clip at the bottom |
| 34 | front | Toggle switch (S1) | C55072606 | Either way round; the lever flips up and down |
| 35 | front | Relay (K1) | C18198081 | Pin 1 / coil end (marked) at the top |
| 36 | front | Power jack (J1) | C381115 | None |
| 37 | front | Check: Plug in the 24 V adapter. C4's + pad: about 24 V |  | Black probe on GND: C4's other pad (black stripe). 0 V: adapter reversed (Q1 blocks it), or check Q1, R1, D2. Over ~26 V: wrong adapter |
| 38 | front | Check: C16's + pad: 5.1–5.5 V |  | The buck's output. Anything else: check R14 (1k), R15 (3.3k), U2, D5, L1. Fit no modules until it is right |
| 39 | back | Check: D4's band end (on the back): 4.8–5.5 V |  | What the DevKit's 5V pin will get. Then unplug the adapter |
| 40 | front | TMC2209 StepStick, then its 9 × 9 × 5 mm heatsink (U4) | on hand | EN pin top-left (INDEX/DIAG pins along the top). Solder on its own pins; keep the heatsink inside the pin rows |
| 41 | front | ESP32-S3 DevKit (U3) | on hand | Antenna at the top, hanging past the edge; USB at the bottom. Solder on its own pins |
| 42 | front | Check: 24 V in: the DevKit's 3V3 pin reads 3.2–3.4 V |  | Top-left pin. Low, or the DevKit gets hot: a short on the 3.3 V parts (U5, U6, U7, U8, the pull-ups) |
| 43 | back | Hall sensor A (top) (U7) | C111925 | Insert into enclosure, then put board on top to get the right pin length. Chamfered side facing left. |
| 44 | back | Hall sensor B (bottom) (U8) | C111925 | Same as U7 |

Also: trim the module pins and the through-hole leads on the back to about 2 mm, so the back stays flat against its standoffs.
