// Collision check: does the board actually fit, and does the lid clear it?
//   openscad -D part="none" assembly_check.scad
include <dps310_case.scad>

module board_block() {
    translate([wall + board_fit, wall + board_fit, floor_t])
        cube([board_w, board_d, board_t]);
}
module lid_assembled() {
    translate([0, 0, h_out]) lid();
}

echo(str("cavity interior: ", in_x, " x ", in_y, " x ", in_z));
echo(str("board:           ", board_w, " x ", board_d, " x ", board_t));
echo(str("side clearance:  ", (in_x - board_w)/2, " mm per side"));
echo(str("space above PCB: ", in_z - board_t, " mm"));
echo(str("lid lip descends to z=", h_out - lip, ", board top is z=", floor_t + board_t,
         " -> clearance ", (h_out - lip) - (floor_t + board_t), " mm"));

show = "section";   // "section" | "cutaway" | "collide" | "collide_lid"

if (show == "cutaway") {
    // slice the roof off so the serpentine is visible from above
    difference() {
        lid();
        translate([-10, -10, lid_t + (box_h - ceiling_t)/2]) cube([80, 80, 20]);
    }
} else if (show == "section") {
    // half the case cut away, with the board and the assembled lid in place
    difference() {
        union() {
            base();
            color("orange") board_block();
            color("lightblue") lid_assembled();
        }
        // remove the NEAR half so the camera looks into the cavity
        translate([-10, -10, -10]) cube([60, out_y/2 + 10, 60]);
    }
} else if (show == "collide") {
    // anything here means interference
    color("red") intersection() { board_block(); base(); }
} else if (show == "collide_lid") {
    color("blue") intersection() { board_block(); lid_assembled(); }
}
