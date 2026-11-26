// =============================================================================
// DPS310 barometric sensor case — offset-baffle static port, no foam
// =============================================================================
// A pressure sensor measures STATIC pressure. Ram air is dynamic pressure: at
// 20 km/h that is 0.19 hPa, about 1.5 m of apparent climb, and at 40 km/h it is
// 6 m. So the case must let the atmosphere in while refusing to let moving air
// in -- an open hole would inject 60x the sensor's own noise, and the error would
// track road speed, which the slope estimator would faithfully report as climb.
//
// Instead of foam, the air has to turn corners. Four offset baffles make a
// serpentine in a box on the lid: the path is long, each corner costs pressure,
// and a gust cannot carry momentum through it. The port through the box wall
// sits low and to the side, so rain has to travel around every corner and uphill
// to reach the sensor.
//
// Print
//   Base: open side up.  Lid: flat face down (baffles grow upward -- no
//   overhangs, no supports). 0.2 mm layers, 3 perimeters, 20% infill.
//   PETG for UV/water; PLA is fine for a fit check but goes brittle in sun/cold.
//
// Assembly
//   1. Board drops into the base; the pocket is a light press fit.
//   2. Lid presses on; the lip holds it. One zip tie round the case locks it
//      and mounts it -- the groove stops the tie sliding off.
//   3. Strap it under the top tube or behind the seat tube, port pointing
//      down/back, in the aerodynamic shadow. Not on the bars.
// =============================================================================

part = "both";   // "base", "lid", "both", or "none" -- override with -D part="..."
$fn = 48;

// --- the board (Adafruit STEMMA QT breakout, 25.4 x 17.8) --------------------
board_w    = 25.4;
board_d    = 17.8;
board_t    = 1.6;
board_fit  = 0.25;    // pocket clearance per side (press fit)
headroom   = 5.0;     // above the PCB: QT connector is ~3.2 mm tall
                      // (also clears a header block mounted on top of the board)

// --- header slot in the floor ------------------------------------------------
// A 7-pin header soldered along one long edge of the board, pins leaving through
// the bottom. The slot is a narrow rectangle under that header only: the rest of
// the floor stays, so the board still rests flat on it.
header_pins   = 7;
header_pitch  = 2.54;
header_slot_l = (header_pins - 1) * header_pitch + 2.4;   // 17.6 mm for 7 pins
header_slot_w = 4.0;      // clears the pins AND lets the plastic block sit in it
header_inset  = 3.0;      // slot centre, measured in from the board edge
header_side   = -1;       // -1 = near long edge, +1 = far long edge
header_shift  = 0.0;      // along the long axis; 0 = centred on the board

// --- shell -------------------------------------------------------------------
wall       = 2.0;
floor_t    = 2.0;
lid_t      = 2.0;
lip        = 1.6;     // lid lip depth into the base
lip_fit    = 0.15;    // clearance; the zip tie is what holds the lid on
corner_r   = 3.0;
tie_w      = 3.2;     // zip-tie groove width
tie_d      = 1.2;     // groove depth into the outer wall

// --- baffle stack ------------------------------------------------------------
baffles    = 4;
baffle_t   = 1.2;
baffle_gap = 5.0;
port_d     = 2.4;
box_l      = 26.0;
box_w      = 16.0;
box_h      = 6.0;
ceiling_t   = 1.6;    // roof over the baffle box: without it the walls do nothing

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
module base() {
    difference() {
        rounded_box(out_x, out_y, h_out, corner_r);
        // cavity (leaves floor + walls). Square corners, not rounded: the board
        // has square corners and a rounded pocket cuts into them -- four slivers
        // of interference, which is exactly enough to stop it dropping in.
        translate([wall, wall, floor_t])
            cube([in_x, in_y, in_z + 1]);
        // zip-tie groove around the outside
        translate([-1, -1, h_out*0.62])
            difference() {
                cube([out_x + 2, out_y + 2, tie_w]);
                translate([tie_d, tie_d, -1])
                    cube([out_x + 2 - 2*tie_d, out_y + 2 - 2*tie_d, tie_w + 2]);
            }
        // header slot: a thin rectangle under the header only
        translate([wall + board_fit + (board_w - header_slot_l)/2 + header_shift,
                   (header_side < 0 ? wall + board_fit + header_inset
                                    : wall + board_fit + board_d - header_inset)
                       - header_slot_w/2,
                   -1])
            cube([header_slot_l, header_slot_w, floor_t + 2]);
    }
}

// -----------------------------------------------------------------------------
module baffle_walls() {
    inner_l = box_l - 2*wall;
    inner_w = box_w - 2*wall;
    step = inner_l / (baffles + 1);
    for (i = [0 : baffles-1]) {
        x = wall + step * (i + 1) - baffle_t/2;
        doy = (i % 2 == 0) ? 0 : baffle_gap;
        translate([x, wall + doy, 0])
            cube([baffle_t, inner_w - baffle_gap, box_h - ceiling_t + 0.01]);
    }
}

module lid() {
    difference() {
        union() {
            rounded_box(out_x, out_y, lid_t, corner_r);
            // locating lip: sits inside the cavity and presses on the walls
            translate([wall + lip_fit, wall + lip_fit, -lip])
                rounded_box(in_x - 2*lip_fit, in_y - 2*lip_fit, lip + 0.02, 1.0);
            // baffle box, open at the top so it prints as a shell
            translate([(out_x - box_l)/2, (out_y - box_w)/2, lid_t])
                difference() {
                    rounded_box(box_l, box_w, box_h, 2.0);
                    // stop 1.6 mm short of the top: that remaining material is
                    // the roof. An open-topped baffle box is a comb the air
                    // flows straight over.
                    translate([wall, wall, -1])
                        cube([box_l - 2*wall, box_w - 2*wall, box_h - ceiling_t + 1]);
                }
            // baffles are SOLID and added back inside the hollow box
            translate([(out_x - box_l)/2, (out_y - box_w)/2, lid_t])
                baffle_walls();
        }
        // into the plenum, at the end of the snake furthest from the outlet
        translate([(out_x - box_l)/2 + wall + 3, out_y/2, -lip])
            cylinder(d = port_d, h = lip + lid_t + 1);
        // outlet to atmosphere, low in the box wall, facing sideways
        translate([(out_x - box_l)/2 + box_l - wall - 1.0, out_y/2 - port_d/2, lid_t + 1.2])
            rotate([0, 90, 0])
                cylinder(d = port_d, h = wall + 2);
    }
}

// -----------------------------------------------------------------------------
if (part == "base")      base();
else if (part == "lid")  lid();
else if (part == "both") { base(); translate([out_x + 8, 0, 0]) lid(); }
// anything else renders nothing, so an including file can draw its own scene
