// =============================================================================
// DPS310 barometer case — rev 3
// sealed chamber, opposed static ports, membrane seats
// =============================================================================
// What changed from rev 2, and why
// --------------------------------
// Rev 2 tried to damp wind with a four-turn serpentine. That cannot work. The
// sensor chamber has one opening, so its pressure equals the pressure at that
// opening; air does not have to flow through the turns for the sensor to see a
// gust. Pressure travels at the speed of sound, and the pneumatic time constant
// of a 2.4 mm hole into a 2 cm3 chamber is a few microseconds. Four baffles add
// microseconds. They filter nothing.
//
// Only two things actually help, and rev 3 does both:
//
//   1. Put the opening where Cp is small and stable, and average two of them.
//      A port in a side wall with the flow parallel to it sits near Cp = 0.
//      Two opposed ports average the antisymmetric yaw term, so a crosswind or
//      a steer does not read as a pressure change. The chamber is the manifold,
//      so no cross-drill is needed.
//
//   2. Restrict the flow with a membrane. R_membrane x C_chamber is the only
//      way to get a real low-pass out of a part this size. A 5 mm ePTFE vent
//      disc in each port gives roughly 0.2 s, which is a genuine filter against
//      gusts and still fast enough for a 4 s grade window. The plastic on its
//      own is microseconds; the discs are what damp the transients.
//
// Both are worthless unless the chamber is sealed, because otherwise the real
// port is the lid joint or the floor. So rev 3 also seals it:
//
//   - flat gasket on the 2 mm rim (1.2 mm silicone sheet, compressed by the
//     straps);
//   - silicone bead around the PCB where it meets the pocket wall;
//   - a single cable hole in the -x end wall, sealed with a grommet or silicone.
//     There is no header slot in the floor any more: the board is wired by its
//     STEMMA QT cable, which exits the -x end. That removes a 70 mm2 opening in
//     the pressure boundary and lets the case sit flat on anything.
//
// The lid is now a flat plate with a locating ring. The baffle box is gone: it
// was there to hold the serpentine, and it also stood the lid 6 mm up into the
// airflow. The two 3.4 mm straps run in grooves in the base's sides and over the
// lid, so they clamp the lid and compress the rim gasket. Two, not one, so the
// gasket is loaded twice and the case cannot rock on the frame tube.
//
// Print
//   Base: open side up. Lid: outside face down (the ring grows upward).
//   0.2 mm layers, 3 perimeters, 20% infill. PETG.
//   No supports. The only overhangs are the 5.5 mm membrane-seat ledges (0.3 mm)
//   and the cable/port bores, all short bridges.
//
// Assembly
//   1. Cut a gasket: 1-1.5 mm silicone sheet, 29.9 x 22.3 outside, 25.9 x 18.3
//      hole. Lay it on the base rim.
//   2. Drop the board in, QT connector toward -x. Run a bead of neutral-cure
//      silicone round the PCB where it meets the pocket wall.
//   3. Feed the QT cable through the 6 mm hole in the -x wall, plug it in, seal
//      the hole (grommet or silicone), and strain-relieve the cable to the
//      frame so the plug is not the only thing holding it.
//   4. Stick a 5 mm ePTFE vent disc into each port seat, adhesive side down.
//      (No disc = a stilling chamber only; the transients are not filtered.)
//   5. Press the lid on. Two 3.4 mm zip ties in the side grooves, round the
//      frame tube, pulled down onto the lid so the gasket compresses.
//   6. Mount with the base toward the tube and the lid outboard, so the straps
//      cross the lid and clamp it. Ports low and to the sides, cable along the
//      tube. Long axis along the frame, not across it.
// =============================================================================

part = "both";        // "base", "lid", "both", "none"
$fn = 48;

// --- board (Adafruit STEMMA QT breakout, 25.4 x 17.8) ------------------------
board_w    = 25.4;
board_d    = 17.8;
board_t    = 1.6;
board_fit  = 0.25;    // pocket clearance per side
headroom   = 5.4;     // PCB top to the lid plate; QT connector is ~3.2 tall

// --- shell -------------------------------------------------------------------
wall       = 2.0;
floor_t    = 2.0;
lid_t      = 2.0;
corner_r   = 3.0;

lip        = 1.5;     // locating ring depth into the cavity
lip_t      = 1.2;     // ring wall thickness
lip_fit    = 0.15;    // clearance to the cavity wall

gasket_t   = 0.6;     // compressed flat gasket (1.0-1.5 mm sheet)

// --- static ports (one per side, opposed) -----------------------------------
port_x     = 15.0;    // mid-length
port_z     = 5.2;     // 1.6 above the PCB, 2.5 below the lid ring
port_d     = 2.5;
seat_d     = 5.5;     // takes a 5 mm ePTFE vent disc
seat_depth = 0.3;
pad_w      = 12.0;
pad_h      = 8.6;
pad_proud  = 1.5;
pad_inset  = 0.2;     // overlap into the wall so the union is solid

// --- strap grooves -----------------------------------------------------------
strap_x    = [6.5, 23.5];
strap_w    = 3.4;
strap_d    = 0.6;     // groove depth; stops 0.6 below the rim so the seal is intact

// --- cable -------------------------------------------------------------------
cable_d    = 6.0;
cable_z    = 5.4;

// -----------------------------------------------------------------------------
in_x  = board_w + 2*board_fit;
in_y  = board_d + 2*board_fit;
in_z  = board_t + headroom;
out_x = in_x + 2*wall;
out_y = in_y + 2*wall;
h_out = floor_t + in_z;

module rounded_box(x, y, z, r) {
    hull() for (dx = [r, x-r], dy = [r, y-r])
        translate([dx, dy, 0]) cylinder(r = r, h = z);
}

// -----------------------------------------------------------------------------
module pads() {
    for (s = [-1, 1])
        translate([port_x - pad_w/2,
                   s < 0 ? -pad_proud : out_y - pad_inset,
                   0])
            cube([pad_w, pad_proud + pad_inset, pad_h]);
}

module port_cuts() {
    for (s = [-1, 1]) {
        // bore into the plenum
        translate([port_x, s < 0 ? 2.1 : out_y - 2.1, port_z])
            rotate([s < 0 ? 90 : -90, 0, 0])
                cylinder(d = port_d, h = 3.8);
        // membrane seat, 0.3 mm deep in the pad face
        translate([port_x, s < 0 ? -pad_proud + seat_depth : out_y + pad_proud - seat_depth, port_z])
            rotate([s < 0 ? 90 : -90, 0, 0])
                cylinder(d = seat_d, h = 0.5);
    }
}

module base() {
    difference() {
        union() {
            rounded_box(out_x, out_y, h_out, corner_r);
            pads();
        }
        // cavity (leaves floor + walls; board pocket is the same cavity)
        translate([wall, wall, floor_t])
            cube([in_x, in_y, in_z + 1]);
        // strap grooves, both sides, stopping short of the rim
        for (x0 = strap_x) {
            translate([x0 - strap_w/2, -1, -1])
                cube([strap_w, strap_d + 1, h_out - strap_d + 1]);
            translate([x0 - strap_w/2, out_y - strap_d, -1])
                cube([strap_w, strap_d + 1, h_out - strap_d + 1]);
        }
        // cable hole in the -x wall
        translate([-1, out_y/2, cable_z])
            rotate([0, 90, 0]) cylinder(d = cable_d, h = 3);
        port_cuts();
    }
}

// -----------------------------------------------------------------------------
module lid() {
    union() {
        rounded_box(out_x, out_y, lid_t, corner_r);
        // locating ring: sits inside the cavity, presses on nothing
        difference() {
            translate([wall + lip_fit, wall + lip_fit, -lip])
                rounded_box(in_x - 2*lip_fit, in_y - 2*lip_fit, lip + 0.01, 1.0);
            translate([wall + lip_fit + lip_t, wall + lip_fit + lip_t, -lip - 1])
                rounded_box(in_x - 2*lip_fit - 2*lip_t,
                            in_y - 2*lip_fit - 2*lip_t,
                            lip + 1.0, 0.5);
        }
    }
}

// -----------------------------------------------------------------------------
if (part == "base")      base();
else if (part == "lid")  lid();
else if (part == "both") { base(); translate([out_x + 8, 0, 0]) lid(); }
