#!/usr/bin/env python3
"""Generate the parametric CDtoSonos Heltec external enclosure.

The source model is deliberately plain CadQuery so dimensions can be adjusted
after the first physical fit check. All dimensions are millimetres.
"""

from __future__ import annotations

import json
from pathlib import Path

import cadquery as cq


ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / "output"

# Carrier PCB and enclosure.
PCB_X = 92.0
PCB_Y = 60.0
PCB_THICKNESS = 1.6
PCB_OFFSET_X = 8.0
PCB_OFFSET_Y = 8.0
OUTER_X = 108.0
OUTER_Y = 76.0
BASE_HEIGHT = 24.0
FLOOR = 2.4
WALL = 2.4
OUTER_RADIUS = 4.0

# Board support and lid fastening.
PCB_STANDOFF = 4.0
PCB_POST_DIAMETER = 7.0
PCB_PILOT_DIAMETER = 2.7
LID_POST_DIAMETER = 6.0
LID_PILOT_DIAMETER = 2.7
LID_POST_EDGE = 4.8

# Lid and OLED window.
LID_TOP = 2.4
LID_SKIRT = 3.5
LID_FIT_CLEARANCE = 0.30
LID_SKIRT_WALL = 1.5
OLED_CENTER_X = 74.4
OLED_CENTER_Y = 38.0
OLED_OPEN_X = 27.5
OLED_OPEN_Y = 15.5
OLED_RECESS_X = 30.4
OLED_RECESS_Y = 18.4
OLED_RECESS_DEPTH = 0.9
WINDOW_INSERT_X = 30.0
WINDOW_INSERT_Y = 18.0
WINDOW_INSERT_Z = 0.75

# Heltec USB-C opening in the rear wall.
USB_CENTER_X = 74.4
USB_BOTTOM_Z = 8.5
USB_OPEN_X = 14.0
USB_OPEN_Z = 9.5

# Replaceable signal-connector plate in the front wall.
PLATE_CENTER_X = 20.0
PLATE_CENTER_Z = 13.0
PLATE_X = 36.0
PLATE_Z = 22.0
PLATE_THICKNESS = 2.4
PLATE_SCREW_X = 15.0
PLATE_SCREW_DIAMETER = 2.8
CASE_CONNECTOR_OPEN_X = 24.0
CASE_CONNECTOR_OPEN_Z = 18.0
PP2010_BARREL_DIAMETER = 16.5


def rounded_box(x: float, y: float, z: float, radius: float) -> cq.Workplane:
    result = cq.Workplane("XY").box(x, y, z, centered=(False, False, False))
    return result.edges("|Z").fillet(radius)


def axis_y_cylinder(
    diameter: float, length: float, x: float, y: float, z: float
) -> cq.Workplane:
    solid = cq.Solid.makeCylinder(
        diameter / 2.0,
        length,
        cq.Vector(x, y, z),
        cq.Vector(0, 1, 0),
    )
    return cq.Workplane(obj=solid)


def base_model() -> cq.Workplane:
    outer = rounded_box(OUTER_X, OUTER_Y, BASE_HEIGHT, OUTER_RADIUS)
    cavity = (
        cq.Workplane("XY")
        .workplane(offset=FLOOR)
        .box(
            OUTER_X - 2 * WALL,
            OUTER_Y - 2 * WALL,
            BASE_HEIGHT - FLOOR + 1,
            centered=(False, False, False),
        )
        .translate((WALL, WALL, 0))
        .edges("|Z")
        .fillet(max(0.8, OUTER_RADIUS - WALL))
    )
    base = outer.cut(cavity)

    # USB-C access is intentionally generous for moulded cable overmoulds.
    usb_cut = (
        cq.Workplane("XY")
        .box(
            USB_OPEN_X,
            WALL + 2,
            USB_OPEN_Z,
            centered=(False, False, False),
        )
        .translate(
            (
                USB_CENTER_X - USB_OPEN_X / 2,
                OUTER_Y - WALL - 1,
                USB_BOTTOM_Z,
            )
        )
    )
    base = base.cut(usb_cut)

    # The connector plate is removable, so a different connector only needs a
    # new 36 x 22 mm plate rather than a complete enclosure reprint.
    connector_open = (
        cq.Workplane("XY")
        .box(
            CASE_CONNECTOR_OPEN_X,
            WALL + 2,
            CASE_CONNECTOR_OPEN_Z,
            centered=(False, False, False),
        )
        .translate(
            (
                PLATE_CENTER_X - CASE_CONNECTOR_OPEN_X / 2,
                -1,
                PLATE_CENTER_Z - CASE_CONNECTOR_OPEN_Z / 2,
            )
        )
    )
    base = base.cut(connector_open)
    for x in (PLATE_CENTER_X - PLATE_SCREW_X, PLATE_CENTER_X + PLATE_SCREW_X):
        base = base.cut(
            axis_y_cylinder(
                PLATE_SCREW_DIAMETER,
                WALL + 2,
                x,
                -1,
                PLATE_CENTER_Z,
            )
        )

    # PCB posts correspond exactly to the carrier's four M3 holes.
    pcb_holes = (
        (PCB_OFFSET_X + 4, PCB_OFFSET_Y + 4),
        (PCB_OFFSET_X + 88, PCB_OFFSET_Y + 4),
        (PCB_OFFSET_X + 4, PCB_OFFSET_Y + 56),
        (PCB_OFFSET_X + 88, PCB_OFFSET_Y + 56),
    )
    for x, y in pcb_holes:
        post = (
            cq.Workplane("XY")
            .workplane(offset=FLOOR)
            .center(x, y)
            .circle(PCB_POST_DIAMETER / 2)
            .extrude(PCB_STANDOFF)
        )
        pilot = (
            cq.Workplane("XY")
            .workplane(offset=FLOOR - 0.1)
            .center(x, y)
            .circle(PCB_PILOT_DIAMETER / 2)
            .extrude(PCB_STANDOFF + 0.2)
        )
        base = base.union(post).cut(pilot)

    # Full-height corner bosses receive short M3 lid screws.
    lid_posts = (
        (LID_POST_EDGE, LID_POST_EDGE),
        (OUTER_X - LID_POST_EDGE, LID_POST_EDGE),
        (LID_POST_EDGE, OUTER_Y - LID_POST_EDGE),
        (OUTER_X - LID_POST_EDGE, OUTER_Y - LID_POST_EDGE),
    )
    for x, y in lid_posts:
        post = (
            cq.Workplane("XY")
            .workplane(offset=FLOOR)
            .center(x, y)
            .circle(LID_POST_DIAMETER / 2)
            .extrude(BASE_HEIGHT - FLOOR)
        )
        pilot = (
            cq.Workplane("XY")
            .workplane(offset=BASE_HEIGHT - 10)
            .center(x, y)
            .circle(LID_PILOT_DIAMETER / 2)
            .extrude(11)
        )
        base = base.union(post).cut(pilot)

    return base


def lid_assembly_model() -> cq.Workplane:
    top = rounded_box(OUTER_X, OUTER_Y, LID_TOP, OUTER_RADIUS).translate(
        (0, 0, BASE_HEIGHT)
    )

    skirt_outer_x = OUTER_X - 2 * (WALL + LID_FIT_CLEARANCE)
    skirt_outer_y = OUTER_Y - 2 * (WALL + LID_FIT_CLEARANCE)
    skirt_outer = (
        rounded_box(
            skirt_outer_x,
            skirt_outer_y,
            LID_SKIRT,
            max(0.8, OUTER_RADIUS - WALL),
        )
        .translate(
            (
                WALL + LID_FIT_CLEARANCE,
                WALL + LID_FIT_CLEARANCE,
                BASE_HEIGHT - LID_SKIRT,
            )
        )
    )
    skirt_inner = (
        cq.Workplane("XY")
        .box(
            skirt_outer_x - 2 * LID_SKIRT_WALL,
            skirt_outer_y - 2 * LID_SKIRT_WALL,
            LID_SKIRT + 0.2,
            centered=(False, False, False),
        )
        .translate(
            (
                WALL + LID_FIT_CLEARANCE + LID_SKIRT_WALL,
                WALL + LID_FIT_CLEARANCE + LID_SKIRT_WALL,
                BASE_HEIGHT - LID_SKIRT - 0.1,
            )
        )
    )
    lid = top.union(skirt_outer.cut(skirt_inner))

    # OLED aperture with a shallow underside recess for clear PETG or sheet.
    oled_through = (
        cq.Workplane("XY")
        .box(
            OLED_OPEN_X,
            OLED_OPEN_Y,
            LID_TOP + 2,
            centered=(False, False, False),
        )
        .translate(
            (
                OLED_CENTER_X - OLED_OPEN_X / 2,
                OLED_CENTER_Y - OLED_OPEN_Y / 2,
                BASE_HEIGHT - 1,
            )
        )
    )
    oled_recess = (
        cq.Workplane("XY")
        .box(
            OLED_RECESS_X,
            OLED_RECESS_Y,
            OLED_RECESS_DEPTH + 0.1,
            centered=(False, False, False),
        )
        .translate(
            (
                OLED_CENTER_X - OLED_RECESS_X / 2,
                OLED_CENTER_Y - OLED_RECESS_Y / 2,
                BASE_HEIGHT - 0.05,
            )
        )
    )
    lid = lid.cut(oled_through).cut(oled_recess)

    # Through holes plus 6.4 mm head recesses on the exterior.
    for x, y in (
        (LID_POST_EDGE, LID_POST_EDGE),
        (OUTER_X - LID_POST_EDGE, LID_POST_EDGE),
        (LID_POST_EDGE, OUTER_Y - LID_POST_EDGE),
        (OUTER_X - LID_POST_EDGE, OUTER_Y - LID_POST_EDGE),
    ):
        # The locating skirt passes close to each full-height base boss. A
        # generous underside relief prevents the skirt binding on the bosses
        # while leaving the exterior screw recess unchanged.
        boss_relief = (
            cq.Workplane("XY")
            .workplane(offset=BASE_HEIGHT - LID_SKIRT - 0.1)
            .center(x, y)
            .circle((LID_POST_DIAMETER + 0.8) / 2)
            .extrude(LID_SKIRT + 0.2)
        )
        through = (
            cq.Workplane("XY")
            .workplane(offset=BASE_HEIGHT - 0.2)
            .center(x, y)
            .circle(3.4 / 2)
            .extrude(LID_TOP + 0.4)
        )
        head = (
            cq.Workplane("XY")
            .workplane(offset=BASE_HEIGHT + LID_TOP - 1.2)
            .center(x, y)
            .circle(6.4 / 2)
            .extrude(1.4)
        )
        lid = lid.cut(boss_relief).cut(through).cut(head)

    return lid


def lid_print_model(lid_assembly: cq.Workplane) -> cq.Workplane:
    # Exterior face on the print bed, internal skirt upwards.
    return (
        lid_assembly.translate((0, 0, -(BASE_HEIGHT + LID_TOP)))
        .rotate((0, 0, 0), (1, 0, 0), 180)
        .translate((0, OUTER_Y, 0))
    )


def connector_plate(pp2010: bool) -> cq.Workplane:
    plate = (
        cq.Workplane("XY")
        .box(PLATE_X, PLATE_Z, PLATE_THICKNESS, centered=(False, False, False))
        .edges("|Z")
        .fillet(2.0)
    )
    for x in (
        PLATE_X / 2 - PLATE_SCREW_X,
        PLATE_X / 2 + PLATE_SCREW_X,
    ):
        plate = plate.cut(
            cq.Workplane("XY")
            .center(x, PLATE_Z / 2)
            .circle(PLATE_SCREW_DIAMETER / 2)
            .extrude(PLATE_THICKNESS + 1)
        )
    if pp2010:
        plate = plate.cut(
            cq.Workplane("XY")
            .center(PLATE_X / 2, PLATE_Z / 2)
            .circle(PP2010_BARREL_DIAMETER / 2)
            .extrude(PLATE_THICKNESS + 1)
        )
    return plate


def window_insert() -> cq.Workplane:
    return (
        cq.Workplane("XY")
        .box(
            WINDOW_INSERT_X,
            WINDOW_INSERT_Y,
            WINDOW_INSERT_Z,
            centered=(False, False, False),
        )
        .edges("|Z")
        .fillet(1.2)
    )


def connector_coupon() -> cq.Workplane:
    coupon = (
        cq.Workplane("XY")
        .box(26, 26, 2.4, centered=(False, False, False))
        .edges("|Z")
        .fillet(2)
    )
    return coupon.cut(
        cq.Workplane("XY")
        .center(13, 13)
        .circle(PP2010_BARREL_DIAMETER / 2)
        .extrude(3.4)
    )


def placeholder_pcb() -> cq.Workplane:
    return (
        cq.Workplane("XY")
        .box(PCB_X, PCB_Y, PCB_THICKNESS, centered=(False, False, False))
        .translate(
            (
                PCB_OFFSET_X,
                PCB_OFFSET_Y,
                FLOOR + PCB_STANDOFF,
            )
        )
    )


def placeholder_heltec() -> cq.Workplane:
    # Official overall dimensions. Placement follows the two carrier sockets;
    # it is a clearance body, not a redistributed Heltec production model.
    return (
        cq.Workplane("XY")
        .box(25.5, 50.2, 10.2, centered=(False, False, False))
        .translate((61.7, 12.9, FLOOR + PCB_STANDOFF + PCB_THICKNESS + 3.0))
    )


def export_part(name: str, part: cq.Workplane) -> None:
    cq.exporters.export(part, str(OUTPUT / f"{name}.stl"), tolerance=0.05)
    cq.exporters.export(part, str(OUTPUT / f"{name}.step"))


def write_drill_template() -> None:
    width = 60
    height = 50
    cx = width / 2
    cy = height / 2
    radius = PP2010_BARREL_DIAMETER / 2
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}mm" height="{height}mm"
 viewBox="0 0 {width} {height}">
  <rect width="{width}" height="{height}" fill="white" stroke="black" stroke-width="0.2"/>
  <circle cx="{cx}" cy="{cy}" r="{radius}" fill="none" stroke="black" stroke-width="0.25"/>
  <line x1="{cx-12}" y1="{cy}" x2="{cx+12}" y2="{cy}" stroke="#777" stroke-width="0.15"/>
  <line x1="{cx}" y1="{cy-12}" x2="{cx}" y2="{cy+12}" stroke="#777" stroke-width="0.15"/>
  <text x="{cx}" y="7" text-anchor="middle" font-family="sans-serif" font-size="3">
    PP2010 nominal M16 cutout - verify actual part
  </text>
  <text x="{cx}" y="46" text-anchor="middle" font-family="sans-serif" font-size="2.5">
    Print at 100% / Actual size
  </text>
</svg>
"""
    (OUTPUT / "pp2010-drill-template.svg").write_text(svg, encoding="utf-8")


def shape_bounds(part: cq.Workplane) -> dict[str, float]:
    box = part.val().BoundingBox()
    return {
        "x": round(box.xlen, 3),
        "y": round(box.ylen, 3),
        "z": round(box.zlen, 3),
    }


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    base = base_model()
    lid_assembly = lid_assembly_model()
    lid_print = lid_print_model(lid_assembly)
    pp2010_plate = connector_plate(True)
    blank_plate = connector_plate(False)
    insert = window_insert()
    coupon = connector_coupon()

    export_part("cdtosonos-case-base", base)
    export_part("cdtosonos-case-lid", lid_print)
    export_part("cdtosonos-connector-plate-pp2010", pp2010_plate)
    export_part("cdtosonos-connector-plate-blank", blank_plate)
    export_part("cdtosonos-oled-window-insert", insert)
    export_part("cdtosonos-pp2010-test-coupon", coupon)

    assembly = cq.Assembly(name="CDtoSonos external enclosure")
    assembly.add(base, name="base", color=cq.Color(0.12, 0.12, 0.13))
    assembly.add(
        lid_assembly,
        name="lid",
        color=cq.Color(0.20, 0.20, 0.22, 0.65),
    )
    assembly.add(
        placeholder_pcb(),
        name="carrier_clearance",
        color=cq.Color(0.05, 0.45, 0.20),
    )
    assembly.add(
        placeholder_heltec(),
        name="heltec_clearance",
        color=cq.Color(0.85, 0.85, 0.88),
    )
    assembly.save(str(OUTPUT / "cdtosonos-case-fit-check.step"))

    write_drill_template()
    dimensions = {
        "units": "mm",
        "enclosure_external": [OUTER_X, OUTER_Y, BASE_HEIGHT + LID_TOP],
        "carrier_pcb": [PCB_X, PCB_Y, PCB_THICKNESS],
        "base_bounds": shape_bounds(base),
        "lid_print_bounds": shape_bounds(lid_print),
        "usb_opening": [USB_OPEN_X, USB_OPEN_Z],
        "oled_visible_opening": [OLED_OPEN_X, OLED_OPEN_Y],
        "oled_insert": [WINDOW_INSERT_X, WINDOW_INSERT_Y, WINDOW_INSERT_Z],
        "connector_plate": [PLATE_X, PLATE_Z, PLATE_THICKNESS],
        "pp2010_nominal_cutout": PP2010_BARREL_DIAMETER,
        "heltec_nominal_top_clearance": round(
            BASE_HEIGHT
            - (FLOOR + PCB_STANDOFF + PCB_THICKNESS + 3.0 + 10.2),
            2,
        ),
        "mechanical_status": "prototype - measure real hardware before final print",
    }
    (OUTPUT / "dimensions.json").write_text(
        json.dumps(dimensions, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(dimensions, indent=2))


if __name__ == "__main__":
    main()
