# DPS310 case — offset-baffle static port

Two printed parts, no foam, no screws. Holds an Adafruit STEMMA QT breakout
(25.4 × 17.8 mm) and gives the sensor a still-air path to the atmosphere.

| Part | Size (mm) | Print orientation |
| :--- | :--- | :--- |
| `base.stl` | 29.9 × 22.3 × 8.6 | open side up |
| `lid.stl` | 29.9 × 22.3 × 9.6 | flat face down, baffles upward |

Closed height ≈ 16.6 mm.

## Why baffles instead of foam

A pressure sensor measures **static** pressure. Moving air is dynamic pressure,
`q = ½ρv²`, and 1 hPa is about 8.3 m of height:

| speed | ram pressure | looks like |
| ---: | ---: | ---: |
| 10 km/h | 0.05 hPa | 0.4 m climb |
| 20 km/h | 0.19 hPa | 1.5 m climb |
| 30 km/h | 0.42 hPa | 3.5 m climb |
| 45 km/h | 0.94 hPa | 7.8 m climb |

The sensor's own noise is 0.03 hPa, so an open port would inject twenty to
thirty times the noise *and* the error would track road speed — which the slope
estimator would read as a climb.

Foam fixes that by diffusion, but foam holds water, clogs with dust and its
resistance changes with humidity. A labyrinth does the same job geometrically:
four offset walls force the air through five turns. Each turn costs pressure, so
a gust cannot carry momentum through, while the mean pressure still equalises.
Water has to travel every turn and uphill to reach the sensor.

The side wall has no cable cut-out: the board is wired by its pin header and the
wires leave through a slot in the floor. The outlet is a 2.4 mm hole low in the box wall, facing sideways. Mount the case
so it points down and rearward.

**The box is roofed.** The first revision had the baffles standing in an open-
topped box, which made them decoration: air simply flowed over the top of every
wall. `ceiling_t` closes the box, so the only ways in are the side outlet and
the hole through the plate into the plenum. Do not remove the roof to make the
baffles visible -- `show="cutaway"` in `assembly_check.scad` exists for that.

## Print settings

- 0.2 mm layers, 3 perimeters, 20 % infill
- **PETG** for UV and water resistance. PLA is fine for a fit check but goes
  brittle in sun and cold, and this lives outdoors.
- No supports needed for either part: every wall is vertical, and the lid is
  printed flat-face-down so the baffles grow upward.

## Fit check

`assembly_check.scad` intersects the board's volume with each printed part, so
interference is measured rather than eyeballed:

```bash
openscad -D part="none" -D show="collide" assembly_check.scad        # board vs base
openscad -D part="none" -D show="collide_lid" assembly_check.scad    # board vs lid
openscad -D part="none" -D show="section" -o section.png assembly_check.scad
openscad -D part="none" -D show="cutaway" -o baffles.png assembly_check.scad   # plan view
```

An empty result means no interference. This is how the first revision was
caught: the pocket had rounded internal corners while the board has square
ones, which clipped all four board corners and would have stopped it dropping
in. The pocket is now square-cornered on purpose -- do not round it.

Clearances as shipped: 0.25 mm per side, 5.0 mm above the PCB, and the lid lip
stops 3.4 mm above the board.

## Assembly

1. Drop the board into the base. The pocket is a light press fit — if your
   printer runs loose, one drop of hot glue in a corner is enough.
2. Press the lid on. The lip locates it; friction is not what holds it.
3. Zip tie around the groove to lock it and mount it.
4. Pins leave through the slot in the floor: a narrow rectangle 17.6 x 4.0 mm
   under the header, along one long edge. The rest of the floor stays, so the
   board still rests flat on it. The slot is wide enough that the header's
   plastic block sits down inside it rather than holding the board up.

## Mounting

Under the top tube, behind the seat tube, or in a frame bag — anywhere in the
aerodynamic shadow, **not** on the bars or in the airflow off the front wheel.
Port pointing down/back. Keep it out of direct sun; a hot dark box reads as
descent.

## Tuning

Everything is a parameter at the top of `dps310_case.scad`:

| parameter | effect |
| :--- | :--- |
| `baffles` | more walls = more damping, longer path |
| `baffle_gap` | free gap air goes around; smaller = more resistance |
| `port_d` | outlet diameter |
| `ceiling_t` | roof thickness over the baffle box (removing it opens the box) |
| `headroom` | plenum above the PCB; raise if your QT connector is tall |
| `board_fit` | pocket clearance; lower for a tighter press fit |
| `header_pins` | number of pins; the slot length follows from it |
| `header_inset` | how far the slot centre sits in from that long edge |
| `header_side` | `-1` near long edge, `+1` far long edge |
| `header_shift` | slide the slot along the board if the header is off-centre |

Increasing resistance raises the pneumatic time constant, which damps gust noise
but adds lag to real changes. A ride test will tell you if you overshot: if the
pressure reads low at speed, you have ram air and need more baffling; if the
reading lags the terrain by more than a second or two, you have too much.

## The check that matters

Ride at 10, 20 and 30 km/h for 30 s each on the same flat stretch. Compute the
mean pressure per speed band. Any systematic shift with speed is the enclosure
failing, not the road.
