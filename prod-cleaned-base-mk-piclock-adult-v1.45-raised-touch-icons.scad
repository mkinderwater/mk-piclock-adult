// mk-piclock-adult V1.45 simplified production model
// Adult enclosure derived from the production mk-piclock-kids geometry.
// Released: 2026-07-22
//
// Hardware:
// - 5.5-inch 256x64 OLED on a 159x50mm carrier, secured with four M2 screws.
// - Raspberry Pi Zero, MAX98357A amplifier, 31x28mm speaker, USB-C power insert.
// - Vertical AHT10 module with a two-slot side grille and one M3 mounting screw.
// - 24x24mm touch PCB with three mounting screws.
//
// Touch control:
// - 14mm recessed sensing circle with a 1mm exterior skin.
// - One continuous angled lid-side roof for support-free printing.
// - Original right-side half-circle crescent around the sensing area.
// - Paired raised music-note icons shield the upper-left third screw area.
// - No crescent extension, hook, flare, decorative moon, or forced render operation.
//
// This revision keeps the separate half-circle touch crescent and replaces the
// previous lettering with the paired music-note geometry from the kids model.
// The primary note head remains centred over the upper-left touch screw, while
// the second note sits lower and left with a visible air gap. The AHT10 M3
// mount, OLED opening extension, and tapered OLED screw-head recesses are retained.
// V1.45 increases the exterior height of the paired music notes and the
// right-side touch crescent by 1.0mm to provide more screw-tip clearance.
// Units: mm

$fn = 64;

// Select: "base", "lid", "lid_print", "assembly", "touch_crescent_test", or "aht10_mount_test".
part = "base";

fit_fudge = 0.3;

// Shared production hardware.
self_tap_pilot_d = 1.8;
control_screw_length = 5.0;
control_screw_head_d = 5.0;
control_screw_head_h = 1.6;
control_screw_shank_d = 2.5;
control_pilot_wall_stop = 0.20;


// -----------------------------------------------------------------------------
// Case
// -----------------------------------------------------------------------------

case_w = 173;
case_h = 77;
case_d = 50;

wall = 3;
corner_r = 4;

lid_t = 3;
lip_depth = 6;
lip_clearance = 0.35;
lip_wall = 2.2;

assembly_lid_lift = 8;

// Centre anti-bow reinforcement. The low cross-rib ties the two long walls
// together at the floor. Tapered buttresses stiffen the wall midpoints while
// remaining below the lid-lip keepout and easy to print without support.
center_reinforcement_x = case_w / 2;
center_floor_rib_w = 3.2;
center_floor_rib_h = 4.8;
center_floor_rib_wall_overlap = 0.30;
center_wall_buttress_w_bottom = 10.0;
center_wall_buttress_w_top = 6.0;
center_wall_buttress_depth_bottom = 8.0;
center_wall_buttress_depth_top = 3.5;
center_wall_buttress_top_z = case_d - lip_depth - 1.0;
center_wall_buttress_slice_h = 0.40;

// -----------------------------------------------------------------------------
// OLED module
// -----------------------------------------------------------------------------

oled_pcb_w = 159.0;
oled_pcb_h = 50.0;

// OLED glass outline mounted on the PCB.
oled_glass_w = 148.0;
oled_glass_h = 48.0;

// Bezel measured from the corresponding glass edge to the visible pixels.
oled_bezel_left = 3.5;
oled_bezel_right = 3.5;
oled_bezel_bottom = 8.0;
oled_bezel_top = 2.0;

// Derived visible opening. The asymmetric top and bottom bezels are retained.
oled_view_w =
    oled_glass_w - oled_bezel_left - oled_bezel_right;
oled_view_h =
    oled_glass_h - oled_bezel_bottom - oled_bezel_top;

oled_pcb_t = 1.6;

oled_hole_span_x = 154.0;
oled_hole_span_y = 44.0;

oled_x = (case_w - oled_pcb_w) / 2;
oled_y = (case_h - oled_pcb_h) / 2;

oled_glass_x = oled_x + (oled_pcb_w - oled_glass_w) / 2;
oled_glass_y = oled_y + (oled_pcb_h - oled_glass_h) / 2;

oled_view_x = oled_glass_x + oled_bezel_left;
oled_view_y = oled_glass_y + oled_bezel_bottom;

oled_hole_x_offset = (oled_pcb_w - oled_hole_span_x) / 2;
oled_hole_y_offset = (oled_pcb_h - oled_hole_span_y) / 2;

assert(
    abs(oled_view_w - 141.0) < 0.001
        && abs(oled_view_h - 38.0) < 0.001,
    "OLED bezel dimensions must produce a 141x38mm visible opening."
);

oled_m2_clearance_d = 2.0 + fit_fudge;
oled_m2_head_preview_d = 4.0;

// Increase only the top edge of the visible opening.
oled_view_top_extension = 1.0;

// Tapered recesses on the exterior lid face for the four OLED M2 screws.
oled_m2_recess_d = 3.6;
oled_m2_recess_depth = 1.1;

oled_solder_joint_padding = 3.0;
oled_solder_padding_d = 6.4;

// -----------------------------------------------------------------------------
// Raspberry Pi Zero and amplifier
// -----------------------------------------------------------------------------

pi_w = 65;
pi_h = 30;
pi_corner_r = 3;

// Match the kid-clock placement within the larger adult enclosure.
// Pi: 7.5mm from the right edge and 5.5mm from the lid-side edge.
pi_x = case_w - pi_w - 7.5;
pi_y = case_h - pi_h - 5.5;

pi_holes = [
    [3.5, 3.5],
    [61.5, 3.5],
    [3.5, 26.5],
    [61.5, 26.5]
];

pi_standoff_h = 6;
pi_standoff_od = 5.8;
pi_standoff_pilot_d = self_tap_pilot_d;

pi_gusset_h = 4.5;
pi_gusset_w = 1.6;
pi_gusset_len = 6;

amp_w = 20;
amp_h = 19;
amp_corner_r = 2;
amp_hole_span = 12.5;
// Align the amplifier's two mounting holes with the Pi's left mounting
// column. The board remains left of the Pi with a clear wiring channel.
amp_hole_x = pi_x + pi_holes[0][0];
amp_x = amp_hole_x - amp_w / 2;
amp_y = 9.5;

amp_standoff_h = pi_standoff_h;
amp_standoff_od = pi_standoff_od;
amp_standoff_pilot_d = self_tap_pilot_d;

amp_holes = [
    [amp_w / 2, amp_h / 2 - amp_hole_span / 2],
    [amp_w / 2, amp_h / 2 + amp_hole_span / 2]
];

amp_lower_hole_y = amp_y + amp_h / 2 - amp_hole_span / 2;
amp_upper_hole_y = amp_y + amp_h / 2 + amp_hole_span / 2;

// -----------------------------------------------------------------------------
// Speaker
// -----------------------------------------------------------------------------

speaker_w = 31;
speaker_h = 28;
speaker_center_x = 20.5;
speaker_center_y = 38.5;

speaker_baffle_h = 7;
speaker_mount_h = speaker_baffle_h;
speaker_mount_w = pi_standoff_od;
speaker_mount_pilot_d = self_tap_pilot_d;
speaker_mount_span = 38.0;

speaker_mount_support_w = speaker_mount_w;
speaker_mount_support_h = speaker_baffle_h;
speaker_support_wall_overlap = 0.8;

speaker_slot_w = 2.2;
speaker_slot_h = 22.0;
speaker_slot_gap = 1.25;
speaker_slot_count = 7;

speaker_baffle_wall = 1.4;
speaker_pocket_clearance = 1.0;
speaker_pocket_w = speaker_w + speaker_pocket_clearance;
speaker_pocket_h = speaker_h + speaker_pocket_clearance;
speaker_baffle_outer_w = speaker_pocket_w + speaker_baffle_wall * 2;
speaker_baffle_outer_h = speaker_pocket_h + speaker_baffle_wall * 2;
speaker_baffle_inner_w = speaker_pocket_w;
speaker_baffle_inner_h = speaker_pocket_h;
speaker_baffle_corner_r = 2.0;

speaker_wire_relief_w = 4.0;
speaker_wire_relief_depth = speaker_baffle_wall + 0.8;
speaker_wire_relief_h = speaker_baffle_h * 0.5;

// -----------------------------------------------------------------------------
// Pi connector preview coordinates
// -----------------------------------------------------------------------------

usb_centers_x = [111.5, 124.1];
micro_hdmi_center_x = 153.1;

// All external Pi-side openings, including microSD, remain closed.

// -----------------------------------------------------------------------------
// USB-C power insert
// -----------------------------------------------------------------------------

usb_c_panel_w = 20;
usb_c_panel_h = 7;
usb_c_panel_depth = 2.2;
usb_c_header_cutout_w = 12.2;
usb_c_header_cutout_h = 7;
usb_c_hole_span = 16;
usb_c_mount_pilot_d = pi_standoff_pilot_d;

usb_c_recess_fudge = 0.25;
usb_c_clearance_from_walls = 10.0;

usb_c_panel_x =
    case_w - wall - usb_c_clearance_from_walls - usb_c_panel_w;
usb_c_panel_y = wall + usb_c_clearance_from_walls;
usb_c_panel_center_x = usb_c_panel_x + usb_c_panel_w / 2;
usb_c_panel_center_y = usb_c_panel_y + usb_c_panel_h / 2;
usb_c_panel_z = 0;

usb_c_hole_centers = [
    [usb_c_panel_center_x - usb_c_hole_span / 2, usb_c_panel_center_y],
    [usb_c_panel_center_x + usb_c_hole_span / 2, usb_c_panel_center_y]
];

usb_c_support_rail_w = 1.4;
usb_c_support_rail_h = 5.0;
usb_c_support_end_w = 1.4;

usb_c_connector_preview_w = usb_c_header_cutout_w;
usb_c_connector_preview_d = usb_c_header_cutout_h;
usb_c_connector_preview_h = 6.0;

assert(
    abs(amp_hole_x - (pi_x + pi_holes[0][0])) < 0.001,
    "Amplifier mounts must align with the Pi left mounting column."
);
assert(
    amp_y + amp_h <= pi_y - 8.0,
    "Amplifier enters the Raspberry Pi keepout."
);

// -----------------------------------------------------------------------------
// Markings and rubber feet
// -----------------------------------------------------------------------------

base_part_number = "mk-piClock A-V1.44";
base_part_number_size = 3.4;
base_part_number_h = 0.45;
base_part_number_x = 63.0;
base_part_number_y = case_h / 2;

// Raised assembly labels on the inside floor.
pi_pin_text_size = 3.2;
pi_pin_text_h = 0.45;
pi_pin_text_y = pi_y - 4.0;
pi_pin_1_x = pi_x + pi_w - 4.0;
pi_pin_40_x = pi_x + 4.0;

amp_label = "AMP";
amp_label_size = 3.2;
amp_label_h = 0.45;
amp_label_x = amp_x + amp_w / 2;
amp_label_y = amp_y + amp_h / 2;

// Raised on the lid underside, below the OLED PCB footprint.
// Mirrored so it reads correctly when viewing the removed lid from inside.
lid_part_number = "MK-PiClock-Adult-V1.44";
lid_part_number_size = 3.2;
lid_part_number_h = 0.55;
lid_text_overlap = 0.05;
lid_part_number_x = case_w / 2;
lid_part_number_y = 11.0;

// Lid orientation marks, raised on the underside and clear of the OLED PCB.
lid_orientation_text_size = 3.6;
lid_orientation_text_h = 0.55;
lid_top_text_x = case_w / 2;
lid_top_clear_edge_y = case_h - (wall + lip_clearance + lip_wall);
lid_top_text_y = (oled_y + oled_pcb_h + lid_top_clear_edge_y) / 2;

usb_c_polarity_mark_size = 4.0;
usb_c_polarity_mark_h = 0.45;
usb_c_polarity_mark_y = usb_c_panel_y - 4.0;
usb_c_polarity_plus_x =
    usb_c_panel_center_x - usb_c_hole_span / 2;

rubber_foot_size = 21;
rubber_foot_recess_depth = 1.2;

// Equal front-to-back spacing: bottom margin = centre gap = lid-side margin.
rubber_foot_z_gap =
    (case_d - 2 * rubber_foot_size) / 3;

// Long-driver openings hidden beneath the right rubber feet.
rubber_foot_tool_access_d = 6.975;

rubber_foot_x_positions = [10, 142];

rubber_foot_z_positions = [
    rubber_foot_z_gap,
    rubber_foot_z_gap * 2 + rubber_foot_size
];

// -----------------------------------------------------------------------------
// AHT10 temperature and humidity sensor
// -----------------------------------------------------------------------------

// Board orientation on the inside left wall:
//   X = module thickness, projecting toward the case interior
//   Y = 10.5mm short dimension, front-to-back
//   Z = 15mm long dimension, vertical
// Components face the ventilated wall; wiring exits into the case.
aht10_pcb_y_size = 10.5;
aht10_pcb_z_size = 15.0;

// Measured module stack:
//   sensor package projection = 1.5mm
//   complete PCB-plus-sensor thickness = 3.2mm
// Therefore the PCB itself is treated as 1.7mm thick.
aht10_sensor_package_h = 1.5;
aht10_module_total_t = 3.2;
aht10_pcb_t = aht10_module_total_t - aht10_sensor_package_h;

// Add 1.5mm of bracket depth toward the case interior. This increases
// screw-tip shielding by moving the PCB mounting plane inward while leaving
// the exterior case wall unchanged.
aht10_sensor_wall_gap = 0.02;
aht10_inner_extension = 1.50;
aht10_standoff_h =
    aht10_sensor_package_h
    + aht10_sensor_wall_gap
    + aht10_inner_extension;
aht10_pcb_outer_x = wall + aht10_standoff_h;
aht10_pcb_inner_x = aht10_pcb_outer_x + aht10_pcb_t;
aht10_sensor_face_x = aht10_pcb_outer_x - aht10_sensor_package_h;
aht10_sensor_actual_wall_gap = aht10_sensor_face_x - wall;

// The sensing package, not the PCB outline, is centred on the left side of
// the enclosure. The board position is derived from that datum.
aht10_sensor_center_y = case_h / 2;
aht10_sensor_center_z = case_d / 2 + 10.0;
aht10_sensor_preview_y = 4.0;
aht10_sensor_preview_z = 5.0;
// Shift only the grille 1mm toward the rubber-foot face.
aht10_vent_center_y = aht10_sensor_center_y - 1.0;
aht10_vent_center_z = aht10_sensor_center_z;

// Relative positions measured from the pictured module. The mounting hole
// remains in its original position directly left of the sensor.
aht10_sensor_from_pcb_center_y = 3.0;
aht10_sensor_from_pcb_center_z = 3.1;
aht10_sensor_to_mount_y = 6.3;

aht10_center_y =
    aht10_sensor_center_y - aht10_sensor_from_pcb_center_y;
aht10_center_z =
    aht10_sensor_center_z - aht10_sensor_from_pcb_center_z;
aht10_pcb_y = aht10_center_y - aht10_pcb_y_size / 2;
aht10_pcb_z = aht10_center_z - aht10_pcb_z_size / 2;

aht10_mount_y =
    aht10_sensor_center_y - aht10_sensor_to_mount_y;
aht10_mount_z = aht10_sensor_center_z;
aht10_mount_hole_preview_d = 3.2;

// Shared support-free screw mount. Its flat face stops at the component-side
// PCB surface. The board sits directly against this face; no locating post or
// insert enters the 3.2mm mounting hole.
aht10_boss_od = pi_standoff_od;
// 2.5mm printed pilot for an M3 self-tapping screw. The AHT10 PCB already
// provides a 3.2mm clearance hole for the M3 shank.
aht10_screw_nominal_d = 3.0;
aht10_screw_length = 5.0;
aht10_pilot_d = 2.5;
aht10_boss_root_drop = aht10_standoff_h;
aht10_boss_slice_x = 0.35;

// The full-height right guide locates the PCB. The short left wall sits
// completely left of the screw boss rather than intersecting it.
aht10_side_clearance = 0.25;
aht10_mount_side_gap = 0.25;
aht10_side_wall_t = 1.2;
aht10_side_wall_z_margin = 0.25;
aht10_side_wall_z = aht10_pcb_z - aht10_side_wall_z_margin;
aht10_side_wall_h =
    aht10_pcb_z_size + 2 * aht10_side_wall_z_margin;
aht10_side_wall_top_z = aht10_side_wall_z + aht10_side_wall_h;

// Guides extend to the rear PCB face. The screw mount ends at the component-side
// PCB face so the board rests on one flat mounting surface. Its upper edge is
// extended to the same Z height as the two side guides.
aht10_side_wall_x_end = aht10_pcb_inner_x;
aht10_mount_x_end = aht10_pcb_outer_x;
aht10_mount_bottom_z = aht10_mount_z - aht10_boss_od / 2;
aht10_mount_top_z = aht10_side_wall_top_z;
aht10_mount_body_h = aht10_mount_top_z - aht10_mount_bottom_z;
// The guide beside the screw mount rises only until it meets the boss.
aht10_mount_side_wall_h =
    aht10_mount_bottom_z - aht10_side_wall_z;
aht10_pilot_depth = aht10_mount_x_end - wall + 0.10;
aht10_screw_min_outer_skin =
    aht10_pcb_inner_x - aht10_screw_length;
aht10_boss_left_y = aht10_mount_y - aht10_boss_od / 2;
aht10_side_wall_left_y =
    aht10_boss_left_y
    - aht10_mount_side_gap
    - aht10_side_wall_t;
aht10_side_wall_right_y =
    aht10_pcb_y + aht10_pcb_y_size + aht10_side_clearance;
aht10_support_slice_x = 0.35;

// Third guide closes only the bottom of the pocket. Its top ends at the
// lower edge of the two vertical guides, preserving 0.25mm PCB clearance.
aht10_bottom_wall_t = aht10_side_wall_t;
aht10_bottom_wall_y = aht10_side_wall_left_y;
aht10_bottom_wall_span_y =
    aht10_side_wall_right_y
    + aht10_side_wall_t
    - aht10_side_wall_left_y;
aht10_bottom_wall_z = aht10_side_wall_z - aht10_bottom_wall_t;

// Two narrow capsule-ended slots match the speaker grille design while
// providing outside air directly to the sensing package.
aht10_vent_count = 2;
aht10_vent_w = 1.8;
aht10_vent_h = 7.0;
aht10_vent_spacing = 2.3;

// Require at least 0.02mm separation from the screw mount and side guide.
aht10_grille_mount_clearance = 0.02;
aht10_grille_left_y =
    aht10_vent_center_y
    - (aht10_vent_count - 1) / 2 * aht10_vent_spacing
    - aht10_vent_w / 2;
aht10_grille_right_y =
    aht10_vent_center_y
    + (aht10_vent_count - 1) / 2 * aht10_vent_spacing
    + aht10_vent_w / 2;
aht10_boss_right_y = aht10_mount_y + aht10_boss_od / 2;

assert(
    abs(aht10_pcb_t + aht10_sensor_package_h - aht10_module_total_t) < 0.001,
    "AHT10 PCB and sensor stack must total 3.2mm."
);
assert(
    abs(
        aht10_sensor_actual_wall_gap
        - (aht10_sensor_wall_gap + aht10_inner_extension)
    ) < 0.001,
    "AHT10 bracket must project 1.5mm farther toward the interior."
);
assert(
    abs(aht10_sensor_center_y - case_h / 2) < 0.001
        && abs(aht10_sensor_center_z - (case_d / 2 + 10.0)) < 0.001,
    "AHT10 sensor must remain centred horizontally and 10mm toward the lid."
);
assert(
    aht10_mount_y > aht10_pcb_y
        && aht10_mount_y < aht10_pcb_y + aht10_pcb_y_size
        && aht10_mount_z > aht10_pcb_z
        && aht10_mount_z < aht10_pcb_z + aht10_pcb_z_size,
    "AHT10 mounting hole falls outside the PCB."
);
assert(
    abs(
        (aht10_sensor_center_y - aht10_mount_y)
        - aht10_sensor_to_mount_y
    ) < 0.001
        && abs(aht10_sensor_center_z - aht10_mount_z) < 0.001,
    "AHT10 mounting hole must remain in its original position."
);
assert(
    abs((aht10_sensor_center_y - aht10_vent_center_y) - 1.0) < 0.001
        && abs(aht10_vent_center_z - aht10_sensor_center_z) < 0.001,
    "AHT10 grille must move 1mm toward the rubber feet only."
);
assert(
    aht10_mount_side_wall_h > 0,
    "AHT10 mount-side guide height must remain positive."
);
assert(
    aht10_side_wall_left_y + aht10_side_wall_t
        <= aht10_boss_left_y - aht10_mount_side_gap + 0.001,
    "AHT10 left wall intersects the screw mount."
);
assert(
    aht10_grille_left_y - aht10_boss_right_y
        >= aht10_grille_mount_clearance,
    "AHT10 grille is too close to the screw mount."
);
assert(
    aht10_side_wall_right_y - aht10_grille_right_y
        >= aht10_grille_mount_clearance,
    "AHT10 right guide overlaps the grille."
);
assert(
    abs(aht10_mount_x_end - aht10_pcb_outer_x) < 0.001,
    "AHT10 screw mount must finish flush with the component-side PCB face."
);
assert(
    abs(aht10_mount_top_z - aht10_side_wall_top_z) < 0.001,
    "AHT10 screw mount and right guide must end at the same height."
);
assert(
    abs(
        aht10_side_wall_z + aht10_mount_side_wall_h
        - aht10_mount_bottom_z
    ) < 0.001,
    "AHT10 mount-side guide must stop at the lower edge of the mount."
);
assert(
    abs(
        aht10_bottom_wall_z + aht10_bottom_wall_t
        - aht10_side_wall_z
    ) < 0.001,
    "AHT10 lower cross-wall must meet the two side guides."
);
assert(
    abs(aht10_standoff_h - (3.02)) < 0.001,
    "AHT10 screw shielding must include the 1.5mm inward extension."
);
assert(
    aht10_screw_min_outer_skin >= 2.70,
    "AHT10 5mm M3 screw leaves insufficient exterior wall shielding."
);
assert(
    aht10_side_wall_top_z < case_d - lip_depth,
    "AHT10 side guides enter the lid-lip keepout."
);

// -----------------------------------------------------------------------------
// Touch sensor
// -----------------------------------------------------------------------------

touch_pcb_size = 24.0;
touch_pcb_t = 1.6;
touch_center_x = 146.0;
touch_center_z = 29.0;

touch_pad_center_x = 151.0;
touch_pad_center_z = 29.0;
touch_pad_d = 14.0;

// Upper-left, upper-right, lower-right. Lower-left remains open for soldering.
touch_mount_points = [
    [136.0, 39.0],
    [156.0, 39.0],
    [156.0, 19.0]
];

// Only the two right screws use exterior driver bores.
touch_tool_access_points = [
    [156.0, 38.0],
    [156.0, 18.0]
];

touch_mount_pilot_d = self_tap_pilot_d;
touch_mount_pilot_depth = wall - control_pilot_wall_stop;
touch_tool_access_d = rubber_foot_tool_access_d;
touch_screw_head_d = control_screw_head_d;
touch_screw_head_h = control_screw_head_h;
touch_screw_shank_d = control_screw_shank_d;

touch_solder_x_size = 3.0;
touch_solder_z_size = 8.0;
touch_solder_clearance = 1.7;
touch_solder_center_x = 136.7;
touch_solder_center_z = 29.0;

touch_panel_inner_y = case_h - wall;
touch_pcb_outer_face_y = touch_panel_inner_y;

// Recess and exterior chamfer.
touch_sensing_skin_thickness = 1.0;
touch_circle_recess_depth = wall - touch_sensing_skin_thickness;
touch_button_d = 14.0;
touch_chamfer_width = 0.4;
touch_chamfer_depth = 0.4;
touch_chamfer_outer_d = touch_button_d + 2 * touch_chamfer_width;
touch_circle_fn = 96;
touch_recess_lid_roof_drop = 1.40;
control_feature_slice_depth = 0.12;

// Original tapered right-side half-circle crescent. Root dimensions are against
// the case wall; face dimensions are at the finished exterior surface.
touch_surround_h = 3.0;
touch_surround_slice = 0.12;
touch_surround_fn = 96;
touch_band_face_w = 2.40;

touch_right_screw_r = sqrt(
    pow(touch_mount_points[1][0] - touch_pad_center_x, 2)
    + pow(touch_mount_points[1][1] - touch_pad_center_z, 2)
);

touch_outer_r_root = touch_right_screw_r + 2.80;
touch_outer_r_face = touch_outer_r_root - touch_surround_h;
touch_inner_r_root = touch_chamfer_outer_d / 2 + 0.60;
touch_inner_r_face = touch_outer_r_face - touch_band_face_w;

// Paired raised eighth-note icons over the upper-left touch screw area. The
// main solid oval note head remains centred over the screw. A second matching
// note sits lower and left with a visible air gap so the raised notes do not
// touch. Looking at the lid exterior, both note tails sit on the right
// and the oval note bases sit closest to the lid.
touch_note_h = 2.60;
touch_note_face_scale = 0.88;
touch_note_stroke_boost = 0.14;
touch_note_head_w = 4.80;
touch_note_head_h = 3.60;
touch_note_head_angle = -18;
touch_note_stem_w = 1.20;
touch_note_stem_h = 6.40;
touch_note_flag_w = 2.80;
touch_note_flag_h = 1.90;
touch_note_origin_x = touch_mount_points[0][0];
touch_note_center_z = touch_mount_points[0][1];
touch_second_note_offset_x = -8.8;
touch_second_note_offset_z = -9.3;
touch_note_pair_min_gap = 0.80;
touch_second_note_origin_x = touch_note_origin_x + touch_second_note_offset_x;
touch_second_note_center_z = touch_note_center_z + touch_second_note_offset_z;

function touch_note_bound_x0(origin_x) = origin_x - touch_note_head_w / 2
    - touch_note_stroke_boost;
function touch_note_bound_x1(origin_x) = origin_x + touch_note_head_w / 2
    + touch_note_flag_w + touch_note_stroke_boost;
function touch_note_bound_z0(center_z) = center_z - touch_note_stem_h
    - touch_note_stroke_boost;
function touch_note_bound_z1(center_z) = center_z + touch_note_head_h / 2
    + touch_note_stroke_boost;

// Conservative bounds used only by the isolated touch-feature test coupon.
// These reflect the right-tail, base-toward-lid orientation below.
touch_note_root_x0 = min([
    touch_note_bound_x0(touch_note_origin_x),
    touch_note_bound_x0(touch_second_note_origin_x)
]);
touch_note_root_x1 = max([
    touch_note_bound_x1(touch_note_origin_x),
    touch_note_bound_x1(touch_second_note_origin_x)
]);
touch_note_root_z0 = min([
    touch_note_bound_z0(touch_note_center_z),
    touch_note_bound_z0(touch_second_note_center_z)
]);
touch_note_root_z1 = max([
    touch_note_bound_z1(touch_note_center_z),
    touch_note_bound_z1(touch_second_note_center_z)
]);

// Printability and screw shielding.
assert(
    touch_recess_lid_roof_drop
        <= touch_circle_recess_depth
            - touch_chamfer_depth
            - control_feature_slice_depth / 2
            + 0.001,
    "Touch recess roof is steeper than 45 degrees."
);
assert(
    touch_inner_r_face >= touch_inner_r_root,
    "Touch opening must widen toward the exterior."
);
assert(
    touch_note_face_scale > 0 && touch_note_face_scale <= 1,
    "Third-screw note face scale must taper inward."
);
assert(
    touch_note_h >= 1.20,
    "Third-screw note icon is too shallow to shield the screw."
);
assert(
    touch_note_head_w >= touch_screw_shank_d + 0.50
        && touch_note_head_h >= touch_screw_shank_d + 0.50,
    "Music-note head is too small to shield the third screw."
);

// Check screw coverage at the depth reached by a 5mm screw through the PCB.
touch_screw_fraction = min(
    max(control_screw_length - touch_pcb_t - wall, 0) / touch_surround_h,
    1
);
touch_screw_required_r = touch_screw_shank_d / 2 + 0.25;
touch_inner_r_at_tip =
    touch_inner_r_root
    + touch_screw_fraction * (touch_inner_r_face - touch_inner_r_root);
touch_outer_r_at_tip =
    touch_outer_r_root
    + touch_screw_fraction * (touch_outer_r_face - touch_outer_r_root);
for (i = [1 : 2]) {
    screw_r = sqrt(
        pow(touch_mount_points[i][0] - touch_pad_center_x, 2)
        + pow(touch_mount_points[i][1] - touch_pad_center_z, 2)
    );

    assert(
        touch_mount_points[i][0] >= touch_pad_center_x
            && screw_r - touch_screw_required_r >= touch_inner_r_at_tip
            && screw_r + touch_screw_required_r <= touch_outer_r_at_tip,
        "Half-circle touch crescent does not shield a right-side screw."
    );
}

assert(
    abs(touch_note_origin_x - touch_mount_points[0][0]) < 0.001
        && abs(touch_note_center_z - touch_mount_points[0][1]) < 0.001,
    "Music-note head is not centred over the upper-left screw."
);

assert(
    touch_second_note_origin_x < touch_note_origin_x
        && touch_second_note_center_z < touch_note_center_z,
    "Second music note must remain lower and left of the main note."
);

assert(
    touch_note_bound_x1(touch_second_note_origin_x) + touch_note_pair_min_gap
        <= touch_note_bound_x0(touch_note_origin_x)
    || touch_note_bound_z1(touch_second_note_center_z) + touch_note_pair_min_gap
        <= touch_note_bound_z0(touch_note_center_z),
    "Music notes touch or overlap; increase second-note offset."
);

assert(
    wall + touch_surround_h - (control_screw_length - touch_pcb_t) >= 1.50,
    "Touch screws exceed the safe 5mm production specification."
);

// -----------------------------------------------------------------------------
// Lid fasteners and supports
// -----------------------------------------------------------------------------

lid_screw_margin = 7;
lid_screw_d = 3.0;
lid_screw_head_d = 6.8;
lid_screw_head_recess_h = 1.8;

lid_mount_pilot_d = 2.65;
lid_mount_pilot_entry_d = 3.05;
lid_mount_pilot_entry_h = 2.2;

lid_mount_top_h = 3.4;
lid_mount_grow_h = 14.4;
lid_mount_h = lid_mount_grow_h + lid_mount_top_h;

lid_mount_base_reach = 1.2;
lid_mount_wall_overlap = 0.55;
lid_mount_inner_extra = 3.5;
lid_mount_reach_scale = 1.30;
lid_mount_lip_clearance = 1.2;
lid_mount_hull_slice_h = 0.25;
lid_mount_blind_pilot_depth = 10.0;

function lid_points() = [
    [lid_screw_margin, lid_screw_margin],
    [case_w - lid_screw_margin, lid_screw_margin],
    [lid_screw_margin, case_h - lid_screw_margin],
    [case_w - lid_screw_margin, case_h - lid_screw_margin]
];

function lid_mount_reach_x(p) =
    (
        p[0] < case_w / 2
        ? p[0] - wall + lid_mount_inner_extra
        : case_w - wall - p[0] + lid_mount_inner_extra
    ) * lid_mount_reach_scale;

function lid_mount_reach_y(p) =
    (
        p[1] < case_h / 2
        ? p[1] - wall + lid_mount_inner_extra
        : case_h - wall - p[1] + lid_mount_inner_extra
    ) * lid_mount_reach_scale;

// -----------------------------------------------------------------------------
// General geometry helpers
// -----------------------------------------------------------------------------

module rounded_cube(size, r) {
    hull() {
        translate([r, r, 0])
            cylinder(h = size[2], r = r);
        translate([size[0] - r, r, 0])
            cylinder(h = size[2], r = r);
        translate([r, size[1] - r, 0])
            cylinder(h = size[2], r = r);
        translate([size[0] - r, size[1] - r, 0])
            cylinder(h = size[2], r = r);
    }
}

module oled_holes() {
    translate([
        oled_x + oled_hole_x_offset,
        oled_y + oled_hole_y_offset,
        0
    ]) children();

    translate([
        oled_x + oled_pcb_w - oled_hole_x_offset,
        oled_y + oled_hole_y_offset,
        0
    ]) children();

    translate([
        oled_x + oled_hole_x_offset,
        oled_y + oled_pcb_h - oled_hole_y_offset,
        0
    ]) children();

    translate([
        oled_x + oled_pcb_w - oled_hole_x_offset,
        oled_y + oled_pcb_h - oled_hole_y_offset,
        0
    ]) children();
}

module pi_mount_holes() {
    for (p = pi_holes)
        translate([pi_x + p[0], pi_y + p[1], 0])
            children();
}

module amp_mount_holes() {
    for (p = amp_holes)
        translate([amp_x + p[0], amp_y + p[1], 0])
            children();
}

// -----------------------------------------------------------------------------
// OLED lid geometry
// -----------------------------------------------------------------------------

module oled_screen_cutout() {
    // Expose only the visible pixel area. The lid covers the glass bezel.
    translate([
        oled_view_x - fit_fudge / 2,
        oled_view_y - fit_fudge / 2,
        -1
    ])
        cube([
            oled_view_w + fit_fudge,
            oled_view_h + fit_fudge + oled_view_top_extension,
            lid_t + 2
        ]);
}

module oled_m2_lid_holes() {
    oled_holes() {
        // Full M2 clearance hole through the lid and solder-joint padding.
        translate([0, 0, -oled_solder_joint_padding - 1])
            cylinder(
                h = lid_t + oled_solder_joint_padding + 2,
                d = oled_m2_clearance_d
            );

        // Exterior tapered recess: 1.1mm deep and 3.6mm wide at the face.
        translate([0, 0, lid_t - oled_m2_recess_depth])
            cylinder(
                h = oled_m2_recess_depth + 0.3,
                d1 = oled_m2_clearance_d,
                d2 = oled_m2_recess_d
            );
    }
}

module oled_half_moon_padding_pad(keep_angle) {
    intersection() {
        translate([0, 0, -oled_solder_joint_padding])
            cylinder(
                h = oled_solder_joint_padding,
                d = oled_solder_padding_d
            );

        rotate([0, 0, keep_angle])
            translate([
                0,
                -oled_solder_padding_d / 2,
                -oled_solder_joint_padding - 0.05
            ])
                cube([
                    oled_solder_padding_d / 2,
                    oled_solder_padding_d,
                    oled_solder_joint_padding + 0.1
                ]);
    }
}

module oled_solder_padding_pads() {
    oled_screen_cx = oled_view_x + oled_view_w / 2;
    oled_screen_cy = oled_view_y + oled_view_h / 2;

    ll_x = oled_x + oled_hole_x_offset;
    lr_x = oled_x + oled_pcb_w - oled_hole_x_offset;
    ly = oled_y + oled_hole_y_offset;
    uy = oled_y + oled_pcb_h - oled_hole_y_offset;

    translate([ll_x, ly, 0])
        oled_half_moon_padding_pad(
            atan2(ly - oled_screen_cy, ll_x - oled_screen_cx)
        );

    translate([lr_x, ly, 0])
        oled_half_moon_padding_pad(
            atan2(ly - oled_screen_cy, lr_x - oled_screen_cx)
        );

    translate([ll_x, uy, 0])
        oled_half_moon_padding_pad(
            atan2(uy - oled_screen_cy, ll_x - oled_screen_cx)
        );

    translate([lr_x, uy, 0])
        oled_half_moon_padding_pad(
            atan2(uy - oled_screen_cy, lr_x - oled_screen_cx)
        );
}

module lid_lip_ring() {
    difference() {
        translate([wall + lip_clearance, wall + lip_clearance, -lip_depth])
            rounded_cube(
                [
                    case_w - 2 * (wall + lip_clearance),
                    case_h - 2 * (wall + lip_clearance),
                    lip_depth
                ],
                max(corner_r - wall, 0.5)
            );

        translate([
            wall + lip_clearance + lip_wall,
            wall + lip_clearance + lip_wall,
            -lip_depth - 0.1
        ])
            rounded_cube(
                [
                    case_w - 2 * (wall + lip_clearance + lip_wall),
                    case_h - 2 * (wall + lip_clearance + lip_wall),
                    lip_depth + 0.2
                ],
                max(corner_r - wall - lip_wall, 0.5)
            );
    }
}

// -----------------------------------------------------------------------------
// Pi and amp supports
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Centre anti-bow reinforcement
// -----------------------------------------------------------------------------

module center_floor_cross_rib() {
    translate([
        center_reinforcement_x - center_floor_rib_w / 2,
        wall - center_floor_rib_wall_overlap,
        wall
    ])
        cube([
            center_floor_rib_w,
            case_h
                - 2 * wall
                + 2 * center_floor_rib_wall_overlap,
            center_floor_rib_h
        ]);
}

module center_wall_buttress(rear = false) {
    y_bottom = rear
        ? case_h - wall - center_wall_buttress_depth_bottom
        : wall - 0.20;
    y_top = rear
        ? case_h - wall - center_wall_buttress_depth_top
        : wall - 0.20;

    hull() {
        translate([
            center_reinforcement_x - center_wall_buttress_w_bottom / 2,
            y_bottom,
            wall
        ])
            cube([
                center_wall_buttress_w_bottom,
                center_wall_buttress_depth_bottom + 0.20,
                center_wall_buttress_slice_h
            ]);

        translate([
            center_reinforcement_x - center_wall_buttress_w_top / 2,
            y_top,
            center_wall_buttress_top_z - center_wall_buttress_slice_h
        ])
            cube([
                center_wall_buttress_w_top,
                center_wall_buttress_depth_top + 0.20,
                center_wall_buttress_slice_h
            ]);
    }
}

module center_reinforcement() {
    center_floor_cross_rib();
    center_wall_buttress();
    center_wall_buttress(true);
}

module pi_standoff_gusset_at(x, y, angle) {
    translate([x, y, wall])
        rotate([0, 0, angle])
            hull() {
                translate([0, -pi_gusset_w / 2, 0])
                    cube([0.1, pi_gusset_w, pi_gusset_h]);

                translate([pi_gusset_len, -pi_gusset_w / 2, 0])
                    cube([0.1, pi_gusset_w, 0.1]);
            }
}

module pi_standoff_gussets() {
    pi_standoff_gusset_at(pi_x + 3.5, pi_y + 3.5, 45);
    pi_standoff_gusset_at(pi_x + 61.5, pi_y + 3.5, 135);
    pi_standoff_gusset_at(pi_x + 3.5, pi_y + 26.5, 315);
    pi_standoff_gusset_at(pi_x + 61.5, pi_y + 26.5, 225);
}

module amp_standoff_gussets() {
    pi_standoff_gusset_at(amp_hole_x, amp_lower_hole_y, 0);
    pi_standoff_gusset_at(amp_hole_x, amp_upper_hole_y, 0);
}

module pi_upper_right_anchor() {
    pi_anchor_d = 2.4;
    pi_anchor_h = 1.8;
    pi_anchor_tip_h = 0.4;
    straight_h = pi_anchor_h - pi_anchor_tip_h;
    anchor_x = pi_x + pi_holes[3][0];
    anchor_y = pi_y + pi_holes[3][1];

    translate([anchor_x, anchor_y, wall + pi_standoff_h]) {
        cylinder(h = straight_h, d = pi_anchor_d);

        translate([0, 0, straight_h])
            cylinder(
                h = pi_anchor_tip_h,
                d1 = pi_anchor_d,
                d2 = pi_anchor_d - 0.6
            );
    }
}

module pi_three_pilot_holes() {
    for (i = [0 : 2]) {
        translate([
            pi_x + pi_holes[i][0],
            pi_y + pi_holes[i][1],
            wall - 1
        ])
            cylinder(
                h = pi_standoff_h + 2,
                d = pi_standoff_pilot_d
            );
    }
}

// -----------------------------------------------------------------------------
// Speaker geometry
// -----------------------------------------------------------------------------

module speaker_mount_points() {
    translate([
        speaker_center_x,
        speaker_center_y - speaker_mount_span / 2,
        0
    ]) children();

    translate([
        speaker_center_x,
        speaker_center_y + speaker_mount_span / 2,
        0
    ]) children();
}

module speaker_support_arm(top = false) {
    y0 = top
        ? speaker_center_y + speaker_baffle_inner_h / 2 + 0.15
        : wall - speaker_support_wall_overlap;
    y1 = top
        ? case_h - wall + speaker_support_wall_overlap
        : speaker_center_y - speaker_baffle_inner_h / 2 - 0.15;

    translate([
        speaker_center_x - speaker_mount_support_w / 2,
        y0,
        wall
    ])
        cube([
            speaker_mount_support_w,
            y1 - y0,
            speaker_mount_support_h
        ]);
}

module speaker_mount_supports() {
    speaker_support_arm();
    speaker_support_arm(true);
}

module speaker_pocket_keepout() {
    translate([
        speaker_center_x - speaker_pocket_w / 2,
        speaker_center_y - speaker_pocket_h / 2,
        wall - 0.05
    ])
        rounded_cube(
            [
                speaker_pocket_w,
                speaker_pocket_h,
                speaker_mount_h + 1.0
            ],
            max(speaker_baffle_corner_r - 0.7, 0.5)
        );
}

module rounded_slot_cutout(w, h, depth) {
    hull() {
        translate([0, -h / 2 + w / 2, 0])
            cylinder(h = depth, d = w);

        translate([0, h / 2 - w / 2, 0])
            cylinder(h = depth, d = w);
    }
}

module speaker_grill_holes() {
    total_w =
        speaker_slot_count * speaker_slot_w
        + (speaker_slot_count - 1) * speaker_slot_gap;

    for (i = [0 : speaker_slot_count - 1]) {
        x =
            -total_w / 2
            + speaker_slot_w / 2
            + i * (speaker_slot_w + speaker_slot_gap);

        translate([speaker_center_x + x, speaker_center_y, -1])
            rounded_slot_cutout(
                speaker_slot_w,
                speaker_slot_h,
                wall + 2
            );
    }
}

module speaker_rect_ring(outer_w, outer_h, inner_w, inner_h, h, r) {
    difference() {
        translate([
            speaker_center_x - outer_w / 2,
            speaker_center_y - outer_h / 2,
            wall
        ])
            rounded_cube([outer_w, outer_h, h], r);

        translate([
            speaker_center_x - inner_w / 2,
            speaker_center_y - inner_h / 2,
            wall - 0.1
        ])
            rounded_cube(
                [inner_w, inner_h, h + 0.2],
                max(r - 0.7, 0.5)
            );
    }
}

module speaker_acoustic_baffle() {
    speaker_rect_ring(
        speaker_baffle_outer_w,
        speaker_baffle_outer_h,
        speaker_baffle_inner_w,
        speaker_baffle_inner_h,
        speaker_baffle_h,
        speaker_baffle_corner_r
    );
}

module speaker_wire_relief_cutout() {
    translate([
        speaker_center_x + speaker_baffle_inner_w / 2 - 0.1,
        speaker_center_y - speaker_wire_relief_w / 2,
        wall + speaker_baffle_h - speaker_wire_relief_h - 0.1
    ])
        cube([
            speaker_wire_relief_depth,
            speaker_wire_relief_w,
            speaker_wire_relief_h + 0.2
        ]);
}

// -----------------------------------------------------------------------------
// Port and USB-C geometry
// -----------------------------------------------------------------------------

module usb_c_header_mount_points() {
    for (p = usb_c_hole_centers)
        translate([p[0], p[1], 0])
            children();
}

module usb_c_header_floor_recess() {
    translate([
        usb_c_panel_x - usb_c_recess_fudge / 2,
        usb_c_panel_y - usb_c_recess_fudge / 2,
        -0.05
    ])
        cube([
            usb_c_panel_w + usb_c_recess_fudge,
            usb_c_panel_h + usb_c_recess_fudge,
            usb_c_panel_depth + 0.10
        ]);
}

module usb_c_header_body_cutout() {
    translate([
        usb_c_panel_center_x - usb_c_header_cutout_w / 2,
        usb_c_panel_center_y - usb_c_header_cutout_h / 2,
        -1
    ])
        cube([
            usb_c_header_cutout_w,
            usb_c_header_cutout_h,
            wall + 2
        ]);
}

module usb_c_header_mount_holes() {
    usb_c_header_mount_points() {
        translate([0, 0, usb_c_panel_depth - 0.05])
            cylinder(
                h =
                    wall
                    + usb_c_support_rail_h
                    - usb_c_panel_depth
                    + 0.25,
                d = usb_c_mount_pilot_d
            );
    }
}

module usb_c_header_cutouts() {
    usb_c_header_floor_recess();
    usb_c_header_body_cutout();
    usb_c_header_mount_holes();
}

module usb_c_header_supports() {
    cutout_x0 = usb_c_panel_center_x - usb_c_header_cutout_w / 2;
    cutout_x1 = usb_c_panel_center_x + usb_c_header_cutout_w / 2;

    translate([
        usb_c_panel_x - usb_c_support_end_w,
        usb_c_panel_y - usb_c_support_rail_w,
        wall
    ])
        cube([
            usb_c_panel_w + usb_c_support_end_w * 2,
            usb_c_support_rail_w,
            usb_c_support_rail_h
        ]);

    translate([
        usb_c_panel_x - usb_c_support_end_w,
        usb_c_panel_y + usb_c_panel_h,
        wall
    ])
        cube([
            usb_c_panel_w + usb_c_support_end_w * 2,
            usb_c_support_rail_w,
            usb_c_support_rail_h
        ]);

    translate([
        usb_c_panel_x - usb_c_support_end_w,
        usb_c_panel_y,
        wall
    ])
        cube([
            cutout_x0 - usb_c_panel_x + usb_c_support_end_w,
            usb_c_panel_h,
            usb_c_support_rail_h
        ]);

    translate([
        cutout_x1,
        usb_c_panel_y,
        wall
    ])
        cube([
            usb_c_panel_x
                + usb_c_panel_w
                + usb_c_support_end_w
                - cutout_x1,
            usb_c_panel_h,
            usb_c_support_rail_h
        ]);
}

// -----------------------------------------------------------------------------
// Markings and foot recesses
// -----------------------------------------------------------------------------

module base_part_number_text() {
    translate([base_part_number_x, base_part_number_y, wall])
        linear_extrude(height = base_part_number_h)
            text(
                base_part_number,
                size = base_part_number_size,
                halign = "center",
                valign = "center"
            );
}

module pi_pin_number_text() {
    translate([pi_pin_1_x, pi_pin_text_y, wall])
        linear_extrude(height = pi_pin_text_h)
            text(
                "1",
                size = pi_pin_text_size,
                halign = "center",
                valign = "center"
            );

    translate([pi_pin_40_x, pi_pin_text_y, wall])
        linear_extrude(height = pi_pin_text_h)
            text(
                "40",
                size = pi_pin_text_size,
                halign = "center",
                valign = "center"
            );
}

module amp_identification_text() {
    translate([amp_label_x, amp_label_y, wall])
        linear_extrude(height = amp_label_h)
            text(
                amp_label,
                size = amp_label_size,
                halign = "center",
                valign = "center"
            );
}

module lid_part_number_text() {
    translate([
        lid_part_number_x,
        lid_part_number_y,
        -lid_part_number_h + lid_text_overlap
    ])
        linear_extrude(height = lid_part_number_h)
            mirror([1, 0, 0])
                text(
                    lid_part_number,
                    size = lid_part_number_size,
                    halign = "center",
                    valign = "center"
                );
}

module lid_orientation_text() {
    translate([
        lid_top_text_x,
        lid_top_text_y,
        -lid_orientation_text_h + lid_text_overlap
    ])
        linear_extrude(height = lid_orientation_text_h)
            mirror([1, 0, 0])
                text(
                    "TOP",
                    size = lid_orientation_text_size,
                    halign = "center",
                    valign = "center"
                );
}

module usb_c_polarity_marks() {
    translate([usb_c_polarity_plus_x, usb_c_polarity_mark_y, wall])
        linear_extrude(height = usb_c_polarity_mark_h)
            text(
                "+",
                size = usb_c_polarity_mark_size,
                halign = "center",
                valign = "center"
            );
}

module rubber_foot_recesses() {
    for (x = rubber_foot_x_positions) {
        for (z = rubber_foot_z_positions) {
            translate([x, -0.1, z])
                cube([
                    rubber_foot_size,
                    rubber_foot_recess_depth + 0.1,
                    rubber_foot_size
                ]);
        }
    }
}

// -----------------------------------------------------------------------------
// FDM-safe circular touch helper
// -----------------------------------------------------------------------------

module control_feature_circle_slice(
    x_pos,
    y_pos,
    z_pos,
    slice_depth,
    diameter
) {
    translate([x_pos, y_pos, z_pos])
        rotate([-90, 0, 0])
            cylinder(
                h = slice_depth,
                d = diameter,
                $fn = touch_circle_fn
            );
}

module control_feature_lid_clipped_circle_slice(
    x_pos,
    y_pos,
    z_pos,
    slice_depth,
    diameter,
    lid_drop
) {
    radius = diameter / 2;

    // Clip only the edge nearest the lid. The rubber-foot side and both
    // lateral edges retain the original circular profile.
    intersection() {
        control_feature_circle_slice(
            x_pos,
            y_pos,
            z_pos,
            slice_depth,
            diameter
        );

        translate([
            x_pos - radius - 1.0,
            y_pos - 0.01,
            z_pos - radius - 1.0
        ])
            cube([
                diameter + 2.0,
                slice_depth + 0.02,
                diameter + 1.0 - lid_drop
            ]);
    }
}

module fdm_safe_circular_control_cutout(
    x_pos,
    z_pos,
    diameter,
    root_y,
    face_y,
    lid_roof_drop
) {
    // One hull between two thin manifold profiles produces a continuous
    // angled roof. The clipped inner profile removes the circular ceiling
    // nearest the lid; the full outer profile restores the exact 14mm face.
    // This replaces the former stack of overlapping solids that caused CSG
    // normalization to exceed 200,000 elements.
    hull() {
        control_feature_lid_clipped_circle_slice(
            x_pos,
            root_y,
            z_pos,
            control_feature_slice_depth,
            diameter,
            lid_roof_drop
        );

        control_feature_circle_slice(
            x_pos,
            face_y,
            z_pos,
            control_feature_slice_depth,
            diameter
        );
    }
}

// -----------------------------------------------------------------------------
// AHT10 side-wall geometry
// -----------------------------------------------------------------------------

module aht10_supported_mount() {
    // Flat 45-degree-supported mount. The outer face is coplanar with the
    // component-side PCB surface, contains only the blind screw pilot, and
    // extends upward to the same top edge as both side guides.
    hull() {
        translate([
            wall - 0.20,
            aht10_mount_y - aht10_boss_od / 2,
            aht10_mount_bottom_z - aht10_boss_root_drop
        ])
            cube([
                aht10_boss_slice_x + 0.20,
                aht10_boss_od,
                aht10_mount_body_h + aht10_boss_root_drop
            ]);

        translate([
            aht10_mount_x_end - aht10_boss_slice_x,
            aht10_mount_y - aht10_boss_od / 2,
            aht10_mount_bottom_z
        ])
            cube([
                aht10_boss_slice_x,
                aht10_boss_od,
                aht10_mount_body_h
            ]);
    }
}

module aht10_mount_pilot_hole() {
    translate([
        aht10_mount_x_end + 0.05,
        aht10_mount_y,
        aht10_mount_z
    ])
        rotate([0, -90, 0])
            cylinder(
                h = aht10_pilot_depth,
                d = aht10_pilot_d
            );
}

module aht10_supported_side_wall(
    y_pos,
    guide_h = aht10_side_wall_h
) {
    x_projection = aht10_side_wall_x_end - wall;

    // Parameterized guide with the same 45-degree lower gradient as the boss.
    hull() {
        translate([
            wall - 0.20,
            y_pos,
            aht10_side_wall_z - x_projection
        ])
            cube([
                aht10_support_slice_x + 0.20,
                aht10_side_wall_t,
                guide_h + x_projection
            ]);

        translate([
            aht10_side_wall_x_end - aht10_support_slice_x,
            y_pos,
            aht10_side_wall_z
        ])
            cube([
                aht10_support_slice_x,
                aht10_side_wall_t,
                guide_h
            ]);
    }
}

module aht10_supported_bottom_wall() {
    x_projection = aht10_side_wall_x_end - wall;

    // Same 45-degree inward growth as the two vertical guides. The wall
    // closes the bottom only; no matching top cross-wall is generated.
    hull() {
        translate([
            wall - 0.20,
            aht10_bottom_wall_y,
            aht10_bottom_wall_z - x_projection
        ])
            cube([
                aht10_support_slice_x + 0.20,
                aht10_bottom_wall_span_y,
                aht10_bottom_wall_t + x_projection
            ]);

        translate([
            aht10_side_wall_x_end - aht10_support_slice_x,
            aht10_bottom_wall_y,
            aht10_bottom_wall_z
        ])
            cube([
                aht10_support_slice_x,
                aht10_bottom_wall_span_y,
                aht10_bottom_wall_t
            ]);
    }
}

module aht10_side_guides() {
    aht10_supported_side_wall(
        aht10_side_wall_left_y,
        aht10_mount_side_wall_h
    );
    aht10_supported_side_wall(aht10_side_wall_right_y);
    aht10_supported_bottom_wall();
}

module aht10_mount_system() {
    aht10_supported_mount();
    aht10_side_guides();
}

module aht10_vent_slot(y_pos) {
    // Same capsule-ended construction as rounded_slot_cutout(), rotated so
    // the slot passes through the left side wall along X.
    hull() {
        translate([
            -1,
            y_pos,
            aht10_vent_center_z
                - aht10_vent_h / 2
                + aht10_vent_w / 2
        ])
            rotate([0, 90, 0])
                cylinder(
                    h = wall + 2,
                    d = aht10_vent_w
                );

        translate([
            -1,
            y_pos,
            aht10_vent_center_z
                + aht10_vent_h / 2
                - aht10_vent_w / 2
        ])
            rotate([0, 90, 0])
                cylinder(
                    h = wall + 2,
                    d = aht10_vent_w
                );
    }
}

module aht10_vent_holes() {
    for (i = [0 : aht10_vent_count - 1]) {
        y_pos =
            aht10_vent_center_y
            + (i - (aht10_vent_count - 1) / 2)
                * aht10_vent_spacing;
        aht10_vent_slot(y_pos);
    }
}

module aht10_preview() {
    // PCB. Components face the ventilated wall.
    color("teal", 0.45)
        translate([
            aht10_pcb_outer_x,
            aht10_pcb_y,
            aht10_pcb_z
        ])
            cube([
                aht10_pcb_t,
                aht10_pcb_y_size,
                aht10_pcb_z_size
            ]);

    // AHT10 sensing package.
    color("white", 0.70)
        translate([
            aht10_pcb_outer_x - aht10_sensor_package_h,
            aht10_sensor_center_y - aht10_sensor_preview_y / 2,
            aht10_sensor_center_z - aht10_sensor_preview_z / 2
        ])
            cube([
                aht10_sensor_package_h,
                aht10_sensor_preview_y,
                aht10_sensor_preview_z
            ]);

    // Single mounting-hole preview.
    color("silver", 0.50)
        translate([
            aht10_pcb_inner_x + 0.02,
            aht10_mount_y,
            aht10_mount_z
        ])
            rotate([0, -90, 0])
                cylinder(
                    h = aht10_pcb_t + 0.04,
                    d = aht10_mount_hole_preview_d
                );
}

module aht10_mount_test() {
    coupon_y0 = aht10_sensor_center_y - 14;
    coupon_z0 = aht10_sensor_center_z - 16;

    translate([0, -coupon_y0, -coupon_z0])
        difference() {
            union() {
                translate([0, coupon_y0, coupon_z0])
                    cube([wall, 28, 32]);
                aht10_mount_system();
            }

            aht10_vent_holes();
            aht10_mount_pilot_hole();
        }
}

// -----------------------------------------------------------------------------
// Touch-sensor mounting geometry
// -----------------------------------------------------------------------------

module touch_mount_point(index, y_pos = case_h - wall - 1) {
    translate([
        touch_mount_points[index][0],
        y_pos,
        touch_mount_points[index][1]
    ]) children();
}

module touch_blind_pilot_at(x_pos, z_pos) {
    translate([
        x_pos,
        touch_panel_inner_y - 0.05,
        z_pos
    ])
        rotate([-90, 0, 0])
            cylinder(
                h = touch_mount_pilot_depth + 0.05,
                d = touch_mount_pilot_d
            );
}

module touch_sensor_mount_holes() {
    for (p = touch_mount_points)
        touch_blind_pilot_at(p[0], p[1]);
}

module touch_sensor_tool_access() {
    for (p = touch_tool_access_points) {
        translate([p[0], -1, p[1]])
            rotate([-90, 0, 0])
                cylinder(
                    h = wall + 2,
                    d = touch_tool_access_d
                );
    }
}

module touch_sensor_solder_pocket() {
    translate([
        touch_solder_center_x - touch_solder_x_size / 2,
        touch_panel_inner_y - 0.05,
        touch_solder_center_z - touch_solder_z_size / 2
    ])
        cube([
            touch_solder_x_size,
            touch_solder_clearance + 0.05,
            touch_solder_z_size
        ]);
}

module touch_sensor_smooth_recess() {
    // The visible 14mm circle is unchanged. A single angled lid-side roof
    // replaces the circular ceiling while retaining the 1mm sensing skin.
    fdm_safe_circular_control_cutout(
        touch_pad_center_x,
        touch_pad_center_z,
        touch_button_d,
        case_h - touch_circle_recess_depth,
        case_h - touch_chamfer_depth - control_feature_slice_depth / 2,
        touch_recess_lid_roof_drop
    );

    // Preserve the original smooth 0.4mm exterior entry chamfer.
    translate([
        touch_pad_center_x,
        case_h + 0.05,
        touch_pad_center_z
    ])
        rotate([90, 0, 0])
            cylinder(
                h = touch_chamfer_depth + 0.05,
                d1 = touch_chamfer_outer_d,
                d2 = touch_button_d,
                $fn = touch_circle_fn
            );
}

module touch_ring_slice(y_pos, radius, depth) {
    translate([touch_pad_center_x, y_pos, touch_pad_center_z])
        rotate([-90, 0, 0])
            cylinder(
                h = depth,
                r = radius,
                $fn = touch_surround_fn
            );
}

module touch_ring_frustum(root_radius, face_radius, inner = false) {
    root_y = inner ? case_h - 0.20 : case_h - 0.06;
    root_depth = inner
        ? touch_surround_slice + 0.30
        : touch_surround_slice;
    face_y = inner
        ? case_h + touch_surround_h - 0.20
        : case_h + touch_surround_h - touch_surround_slice;
    face_depth = inner
        ? touch_surround_slice + 0.40
        : touch_surround_slice + 0.06;

    hull() {
        touch_ring_slice(root_y, root_radius, root_depth);
        touch_ring_slice(face_y, face_radius, face_depth);
    }
}

module touch_half_crescent_mask() {
    // Keep only the right half of the annular surround. The flat cut passes
    // through the sensing-circle centre, restoring the original half circle.
    translate([
        touch_pad_center_x,
        case_h - 0.40,
        touch_pad_center_z - touch_outer_r_root - 2
    ])
        cube([
            touch_outer_r_root + 3,
            touch_surround_h + 0.80,
            touch_outer_r_root * 2 + 4
        ]);
}

module touch_half_crescent() {
    intersection() {
        difference() {
            touch_ring_frustum(
                touch_outer_r_root,
                touch_outer_r_face
            );

            touch_ring_frustum(
                touch_inner_r_root,
                touch_inner_r_face,
                true
            );
        }

        touch_half_crescent_mask();
    }
}

module touch_note_2d() {
    offset(delta = touch_note_stroke_boost)
        union() {
            // Solid oval head centred over the hidden screw.
            rotate([0, 0, touch_note_head_angle])
                scale([
                    touch_note_head_w / touch_note_head_h,
                    1
                ])
                    circle(d = touch_note_head_h, $fn = 64);

            // Looking at the lid exterior, the oval base is nearest the lid
            // and the right-side tail extends away from it.
            translate([
                touch_note_head_w / 2 - touch_note_stem_w,
                -touch_note_head_h * 0.18
            ])
                square([
                    touch_note_stem_w,
                    touch_note_stem_h + touch_note_head_h * 0.18
                ]);

            // Simple flag, kept thick and broad for reliable FDM printing.
            translate([
                touch_note_head_w / 2 - touch_note_stem_w,
                touch_note_stem_h - touch_note_flag_h
            ])
                polygon(points = [
                    [0, touch_note_flag_h],
                    [touch_note_stem_w + touch_note_flag_w, touch_note_flag_h - 0.60],
                    [touch_note_stem_w + touch_note_flag_w * 0.82, 0],
                    [touch_note_stem_w, 0.30],
                    [touch_note_stem_w, touch_note_flag_h - 0.50]
                ]);
        }
}

module touch_music_note_at(origin_x, center_z) {
    translate([origin_x, case_h - 0.06, center_z])
        rotate([-90, 0, 0])
            linear_extrude(
                height = touch_note_h + 0.06,
                scale = [
                    touch_note_face_scale,
                    touch_note_face_scale
                ],
                convexity = 10
            )
                touch_note_2d();
}

module touch_third_screw_note() {
    touch_music_note_at(touch_note_origin_x, touch_note_center_z);
    touch_music_note_at(touch_second_note_origin_x, touch_second_note_center_z);
}

module touch_surround() {
    touch_half_crescent();
    touch_third_screw_note();
}

module touch_crescent_test() {
    test_margin = 4.0;
    test_x0 = min(
        touch_pad_center_x - touch_outer_r_root,
        touch_note_root_x0
    ) - test_margin;
    test_x1 = max(
        touch_pad_center_x + touch_outer_r_root,
        touch_note_root_x1
    ) + test_margin;
    test_z0 = min(
        touch_pad_center_z - touch_outer_r_root,
        touch_note_root_z0
    ) - test_margin;
    test_z1 = max(
        touch_pad_center_z + touch_outer_r_root,
        touch_note_root_z1
    ) + test_margin;

    translate([-test_x0, -(case_h - wall), -test_z0])
        difference() {
            union() {
                translate([test_x0, case_h - wall, test_z0])
                    cube([
                        test_x1 - test_x0,
                        wall,
                        test_z1 - test_z0
                    ]);

                touch_surround();
            }

            touch_sensor_smooth_recess();
            touch_sensor_mount_holes();
            touch_sensor_solder_pocket();
        }
}

// -----------------------------------------------------------------------------
// Case shell and lid supports
// -----------------------------------------------------------------------------

module case_shell() {
    difference() {
        rounded_cube([case_w, case_h, case_d], corner_r);

        translate([wall, wall, wall])
            rounded_cube(
                [
                    case_w - 2 * wall,
                    case_h - 2 * wall,
                    case_d + 1
                ],
                max(corner_r - wall, 0.5)
            );

        speaker_grill_holes();
        rubber_foot_recesses();
        aht10_vent_holes();
    }
}

module lid_corner_mount_block(p, reach_x, reach_y, zpos, block_h) {
    x0 = p[0] < case_w / 2
        ? wall - lid_mount_wall_overlap
        : case_w - wall - reach_x;
    y0 = p[1] < case_h / 2
        ? wall - lid_mount_wall_overlap
        : case_h - wall - reach_y;

    translate([x0, y0, zpos])
        cube([
            reach_x + lid_mount_wall_overlap,
            reach_y + lid_mount_wall_overlap,
            block_h
        ]);
}

module lid_corner_mount(p) {
    z0 = case_d - lid_mount_h;
    full_reach_x = lid_mount_reach_x(p);
    full_reach_y = lid_mount_reach_y(p);

    hull() {
        lid_corner_mount_block(
            p,
            lid_mount_base_reach,
            lid_mount_base_reach,
            z0,
            lid_mount_hull_slice_h
        );

        lid_corner_mount_block(
            p,
            full_reach_x,
            full_reach_y,
            z0 + lid_mount_grow_h - lid_mount_hull_slice_h,
            lid_mount_hull_slice_h
        );
    }

    lid_corner_mount_block(
        p,
        full_reach_x,
        full_reach_y,
        z0 + lid_mount_grow_h,
        lid_mount_top_h
    );
}

module lid_lip_corner_mount_relief(p, z0, zh) {
    margin = 1.2;
    reach_x = lid_mount_reach_x(p) + lid_mount_lip_clearance;
    reach_y = lid_mount_reach_y(p) + lid_mount_lip_clearance;
    x0 = p[0] < case_w / 2
        ? wall - margin
        : case_w - wall - reach_x - margin;
    y0 = p[1] < case_h / 2
        ? wall - margin
        : case_h - wall - reach_y - margin;

    translate([x0, y0, z0])
        cube([
            reach_x + 2 * margin,
            reach_y + 2 * margin,
            zh
        ]);
}

module lid_lip_mount_relief() {
    for (p = lid_points())
        lid_lip_corner_mount_relief(
            p,
            -lip_depth - 0.6,
            lip_depth + 1.2
        );
}

module lid() {
    difference() {
        union() {
            rounded_cube([case_w, case_h, lid_t], corner_r);
            lid_lip_ring();
            oled_solder_padding_pads();
            lid_part_number_text();
            lid_orientation_text();
        }

        lid_lip_mount_relief();

        oled_screen_cutout();
        oled_m2_lid_holes();

        for (p = lid_points()) {
            translate([p[0], p[1], -1])
                cylinder(h = lid_t + 2, d = lid_screw_d);

            translate([p[0], p[1], lid_t - lid_screw_head_recess_h])
                cylinder(
                    h = lid_screw_head_recess_h + 0.3,
                    d1 = lid_screw_d,
                    d2 = lid_screw_head_d
                );
        }
    }
}

// -----------------------------------------------------------------------------
// Base
// -----------------------------------------------------------------------------

module base_cutouts() {
    for (p = lid_points()) {
        translate([
            p[0],
            p[1],
            case_d - lid_mount_blind_pilot_depth
        ])
            cylinder(
                d = lid_mount_pilot_d,
                h = lid_mount_blind_pilot_depth + 0.2
            );

        translate([
            p[0],
            p[1],
            case_d - lid_mount_pilot_entry_h
        ])
            cylinder(
                d = lid_mount_pilot_entry_d,
                h = lid_mount_pilot_entry_h + 0.3
            );
    }

    usb_c_header_cutouts();

    touch_sensor_mount_holes();
    touch_sensor_tool_access();
    touch_sensor_solder_pocket();
    touch_sensor_smooth_recess();

    aht10_mount_pilot_hole();

    pi_three_pilot_holes();

    amp_mount_holes() {
        translate([0, 0, wall - 1])
            cylinder(
                h = amp_standoff_h + 2,
                d = amp_standoff_pilot_d
            );
    }

    speaker_mount_points() {
        translate([0, 0, wall - 1])
            cylinder(
                h = speaker_mount_h + 2,
                d = speaker_mount_pilot_d
            );
    }

    speaker_pocket_keepout();
    speaker_wire_relief_cutout();
}

module base() {
    difference() {
        union() {
            case_shell();
            touch_surround();
            aht10_mount_system();

            for (p = lid_points())
                lid_corner_mount(p);

            speaker_acoustic_baffle();
            speaker_mount_supports();
            pi_standoff_gussets();
            amp_standoff_gussets();
            center_reinforcement();
            usb_c_header_supports();
            base_part_number_text();
            pi_pin_number_text();
            amp_identification_text();
            usb_c_polarity_marks();

            pi_mount_holes() {
                translate([0, 0, wall])
                    cylinder(
                        h = pi_standoff_h,
                        d = pi_standoff_od
                    );
            }

            pi_upper_right_anchor();

            amp_mount_holes() {
                translate([0, 0, wall])
                    cylinder(
                        h = amp_standoff_h,
                        d = amp_standoff_od
                    );
            }
        }

        base_cutouts();
    }
}

// -----------------------------------------------------------------------------
// FDM print orientations
// -----------------------------------------------------------------------------

module lid_print_orientation() {
    translate([case_w, 0, lid_t])
        rotate([0, 180, 0])
            lid();
}

// -----------------------------------------------------------------------------
// Assembly previews
// -----------------------------------------------------------------------------

module oled_preview() {
    lid_z = case_d + assembly_lid_lift;

    color("green", 0.35)
        translate([
            oled_x,
            oled_y,
            lid_z - oled_solder_joint_padding - oled_pcb_t
        ])
            cube([oled_pcb_w, oled_pcb_h, oled_pcb_t]);

    color("black", 0.65)
        translate([
            oled_glass_x,
            oled_glass_y,
            lid_z + lid_t - 0.03
        ])
            cube([oled_glass_w, oled_glass_h, 0.06]);

    color("gray", 0.35)
        translate([
            oled_view_x,
            oled_view_y,
            lid_z + lid_t + 0.02
        ])
            cube([oled_view_w, oled_view_h, 0.04]);

    oled_holes() {
        color("silver", 0.45)
            translate([0, 0, lid_z + lid_t + 0.05])
                cylinder(h = 0.6, d = oled_m2_head_preview_d);
    }
}

module pi_zero_preview() {
    color("green", 0.35)
        translate([pi_x, pi_y, wall + pi_standoff_h + 0.3])
            rounded_cube([pi_w, pi_h, 1.6], pi_corner_r);

    for (x = usb_centers_x) {
        color("silver", 0.55)
            translate([
                x - 4.5,
                pi_y + pi_h - 4,
                wall + pi_standoff_h + 2
            ])
                cube([9, 8, 7]);
    }

    color("silver", 0.25)
        translate([
            micro_hdmi_center_x - 5,
            pi_y + pi_h - 4,
            wall + pi_standoff_h + 2
        ])
            cube([10, 8, 5]);
}

module amp_preview() {
    color("blue", 0.35)
        translate([amp_x, amp_y, wall + amp_standoff_h + 0.3])
            rounded_cube([amp_w, amp_h, 1.6], amp_corner_r);
}

module usb_c_header_preview() {
    color("green", 0.35)
        translate([usb_c_panel_x, usb_c_panel_y, usb_c_panel_z])
            cube([
                usb_c_panel_w,
                usb_c_panel_h,
                usb_c_panel_depth
            ]);

    color("silver", 0.45)
        translate([
            usb_c_panel_center_x - usb_c_connector_preview_w / 2,
            usb_c_panel_center_y - usb_c_connector_preview_d / 2,
            usb_c_panel_depth
        ])
            cube([
                usb_c_connector_preview_w,
                usb_c_connector_preview_d,
                usb_c_connector_preview_h
            ]);
}

module touch_sensor_preview() {
    pcb_outer_face_y = touch_pcb_outer_face_y;
    pcb_inner_face_y = pcb_outer_face_y - touch_pcb_t;

    color("green", 0.35)
        translate([
            touch_center_x - touch_pcb_size / 2,
            pcb_inner_face_y,
            touch_center_z - touch_pcb_size / 2
        ])
            cube([
                touch_pcb_size,
                touch_pcb_t,
                touch_pcb_size
            ]);

    color("gold", 0.40)
        translate([
            touch_pad_center_x,
            pcb_outer_face_y + 0.02,
            touch_pad_center_z
        ])
            rotate([-90, 0, 0])
                cylinder(h = 0.08, d = touch_pad_d);

    color("silver", 0.55)
        translate([
            touch_solder_center_x - touch_solder_x_size / 2,
            touch_panel_inner_y,
            touch_solder_center_z - touch_solder_z_size / 2
        ])
            cube([
                touch_solder_x_size,
                touch_solder_clearance,
                touch_solder_z_size
            ]);

    color("silver", 0.45)
        for (i = [0 : len(touch_mount_points) - 1]) {
            touch_mount_point(
                i,
                pcb_inner_face_y - touch_screw_head_h
            ) {
                rotate([-90, 0, 0])
                    cylinder(
                        h = touch_screw_head_h,
                        d = touch_screw_head_d
                    );

                translate([0, touch_screw_head_h - 0.05, 0])
                    rotate([-90, 0, 0])
                        cylinder(
                            h = control_screw_length,
                            d = touch_screw_shank_d
                        );
            }
        }
}

module speaker_preview() {
    color("gray", 0.35)
        translate([
            speaker_center_x - speaker_w / 2,
            speaker_center_y - speaker_h / 2,
            wall + 0.15
        ])
            rounded_cube([speaker_w, speaker_h, 4.0], 2.0);
}

module assembly() {
    base();

    translate([0, 0, case_d + assembly_lid_lift])
        lid();

    oled_preview();
    pi_zero_preview();
    amp_preview();
    usb_c_header_preview();
    touch_sensor_preview();
    aht10_preview();
    speaker_preview();
}

// -----------------------------------------------------------------------------
// Output
// -----------------------------------------------------------------------------

if (part == "base") {
    base();
}
else if (part == "lid") {
    lid();
}
else if (part == "lid_print") {
    lid_print_orientation();
}
else if (part == "assembly") {
    assembly();
}
else if (part == "touch_crescent_test") {
    touch_crescent_test();
}
else if (part == "aht10_mount_test") {
    aht10_mount_test();
}
else {
    assert(false, str("Unknown part selector: ", part));
}
