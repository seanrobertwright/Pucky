# Elecrow 2.1" rotary module — geometry from the vendor STEP

Source: `3D file/00-irad-21.stp` in Elecrow's repo, tessellated and sectioned on 2026-09-12
(scripts in the session scratchpad; render in `elecrow21-module-render.png`). Axes are the STEP's:
**+Y is the screen side**, X/Z span the face. The STEP models the bare module (knob puck + encoder +
small PCB), not a finished housing.

| Feature | Value | Y range |
|---|---|---|
| Knob/bezel puck | Ø79.0, 14.0 thick | −1.6 … +12.4 |
| Encoder boss, step 1 | Ø23.6 | −1.6 … −7.0 |
| Encoder boss, step 2 | Ø21.4 | −7.0 … −11.5 |
| Encoder boss, step 3 | Ø16.9 | −11.5 … −13.5 |
| Mounting plate | Ø48.0, 1.0 thick | −13.5 … −14.5 |
| PCB | 40.4 × 38.3 (X −22.1…18.3, Z −20.5…17.8) | −14.5 … −16.5 |
| Rear components | region X −14…18, Z −7…18, up to 4.5 tall | −16.5 … −21.0 |
| Overall | Ø79.0 × 33.4 deep | −21.0 … +12.4 |

Rear component occupancy (4 mm cells, deeper than Y −16.6; rows Z +20 → −24, cols X −24 → +20):

```
z=  16 ...######...
z=  12 ...######...
z=   8 ......#.....
z=   4 .....##..##.
z=   0 ..##..#..##.
z=  -4 ..######.##.
z=  -8 ....####.##.
```

Datasheets in the repo name the encoder (EC28A hollow-shaft), the LCD FPC connector
(X05C2012), a 4-pin 1.25 mm wire-to-board connector (ZX-MX1.25-4P) and the CST826 touch IC.
