// Fit and interference checks for rev 3.
//   openscad -D part="none" assembly_check.scad
//   openscad -D part="none" -D show="collide"     assembly_check.scad   # board vs base
//   openscad -D part="none" -D show="collide_lid" assembly_check.scad   # board vs lid
//   openscad -D part="none" -D show="collide_qt"  assembly_check.scad   # QT connector vs lid
//   openscad -D part="none" -D show="ports" -o ports.png assembly_check.scad
//   openscad -D part="none" -D show="cable" -o cable.png assembly_check.scad
include <dps310_case.scad>

board_x = wall + board_fit;
board_y = wall + board_fit;

module board_block() {
    translate([board_x, board_y, floor_t]) cube([board_w, board_d, board_t]);
}
// STEMMA QT connector at the -x end of the board, on top.
module qt_block() {
    translate([board_x, board_y + (board_d - 4.5)/2, floor_t + board_t])
        cube([5.0, 4.5, 3.2]);
}
module lid_assembled() {
    translate([0, 0, h_out + gasket_t]) lid();
}

echo(str("cavity interior: ", in_x, " x ", in_y, " x ", in_z));
echo(str("shell:           ", out_x, " x ", out_y, " x ", h_out, "  (+", pad_proud, " pads/side, +", lid_t, " lid, +", gasket_t, " gasket)"));
echo(str("plenum over PCB: ", in_z - board_t, " mm  (ring stops at ", lip, " mm)"));
echo(str("QT top to ring:  ", (h_out + gasket_t - lip) - (floor_t + board_t + 3.2), " mm"));
echo(str("port bore:       z = ", port_z, " +- ", port_d/2, "  (PCB top ", floor_t + board_t, ", ring bottom ", h_out + gasket_t - lip, ")"));
echo(str("port disc:       d = ", seat_d, " seat, disc 5 mm"));
echo(str("cable hole:      d = ", cable_d, " at z = ", cable_z));
echo(str("strap grooves:   x = ", strap_x, ", ", strap_w, " wide, ", strap_d, " deep, stop at z = ", h_out - strap_d));

show = "section";

if (show == "collide") {
    color("red") intersection() { board_block(); base(); }
} else if (show == "collide_lid") {
    color("blue") intersection() { board_block(); lid_assembled(); }
} else if (show == "collide_qt") {
    color("magenta") intersection() { qt_block(); lid_assembled(); }
} else if (show == "ports") {
    // section through both static ports
    difference() {
        union() {
            base();
            color("orange") board_block();
            color("cyan") qt_block();
            color("lightblue") lid_assembled();
        }
        translate([port_x, out_y/2, -10]) cube([40, 60, 60]);
    }
} else if (show == "cable") {
    // section through the cable hole
    difference() {
        union() {
            base();
            color("orange") board_block();
            color("cyan") qt_block();
            color("lightblue") lid_assembled();
        }
        translate([-10, -10, -10]) cube([60, out_y/2 + 10, 60]);
    }
} else if (show == "parts") {
    color("BurlyWood") base();
    translate([out_x + 8, 0, 0]) color("SteelBlue") lid();
} else {
    // assembly, no cut
    union() {
        color("BurlyWood") base();
        color("orange") board_block();
        color("cyan") qt_block();
        color("SteelBlue") lid_assembled();
    }
}
