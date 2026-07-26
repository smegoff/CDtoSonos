"""Generate the CDtoSonos Heltec V3 carrier in KiCad 10 format.

The generated PCB deliberately uses only standard KiCad footprints so it can
be regenerated and checked without third-party libraries.  The Heltec module
is represented by its two 1x18, 2.54 mm socket rows at the official 22.86 mm
row spacing.
"""

from __future__ import annotations

import os
import zlib
from pathlib import Path
from uuid import uuid4

import pcbnew
from kiutils.items.common import Effects, Fill, Font, Position, Property, Stroke, TitleBlock
from kiutils.items.schitems import Connection, GlobalLabel, NoConnect, SchematicSymbol, SymbolInstance
from kiutils.items.syitems import SyRect
from kiutils.schematic import Schematic
from kiutils.symbol import Symbol, SymbolLib, SymbolPin


ROOT = Path(__file__).resolve().parent
KICAD_SHARE = Path(os.environ["LOCALAPPDATA"]) / "Programs" / "KiCad" / "10.0" / "share" / "kicad"
FOOTPRINTS = KICAD_SHARE / "footprints"
BOARD_PATH = ROOT / "cdtosonos-heltec-carrier.kicad_pcb"
LEGACY_SCH_PATH = ROOT / "cdtosonos-heltec-carrier.sch"
SCHEMATIC_PATH = ROOT / "cdtosonos-heltec-carrier.kicad_sch"
SYMBOL_TABLE_PATH = ROOT / "sym-lib-table"
FOOTPRINT_TABLE_PATH = ROOT / "fp-lib-table"


def point(x: float, y: float) -> pcbnew.VECTOR2I:
    return pcbnew.VECTOR2I_MM(x, y)


def add_footprint(
    board: pcbnew.BOARD,
    library: str,
    name: str,
    reference: str,
    value: str,
    x: float,
    y: float,
    rotation: float = 0,
) -> pcbnew.FOOTPRINT:
    footprint = pcbnew.FootprintLoad(str(FOOTPRINTS / f"{library}.pretty"), name)
    if footprint is None:
        raise RuntimeError(f"Unable to load {library}:{name}")
    footprint.SetFPID(pcbnew.LIB_ID(library, name))
    footprint.SetReference(reference)
    footprint.SetValue(value)
    footprint.SetPosition(point(x, y))
    footprint.SetOrientationDegrees(rotation)
    board.Add(footprint)
    return footprint


def add_track(
    board: pcbnew.BOARD,
    net: pcbnew.NETINFO_ITEM,
    points: list[tuple[float, float]],
    layer: int = pcbnew.F_Cu,
    width: float = 0.35,
) -> None:
    for start, end in zip(points, points[1:]):
        track = pcbnew.PCB_TRACK(board)
        track.SetStart(point(*start))
        track.SetEnd(point(*end))
        track.SetWidth(pcbnew.FromMM(width))
        track.SetLayer(layer)
        track.SetNet(net)
        board.Add(track)


def add_via(
    board: pcbnew.BOARD,
    net: pcbnew.NETINFO_ITEM,
    x: float,
    y: float,
    diameter: float = 0.8,
    drill: float = 0.4,
) -> None:
    via = pcbnew.PCB_VIA(board)
    via.SetPosition(point(x, y))
    via.SetWidth(pcbnew.FromMM(diameter))
    via.SetDrill(pcbnew.FromMM(drill))
    via.SetNet(net)
    board.Add(via)


def add_edge(board: pcbnew.BOARD, start: tuple[float, float], end: tuple[float, float]) -> None:
    shape = pcbnew.PCB_SHAPE(board)
    shape.SetShape(pcbnew.SHAPE_T_SEGMENT)
    shape.SetStart(point(*start))
    shape.SetEnd(point(*end))
    shape.SetLayer(pcbnew.Edge_Cuts)
    shape.SetWidth(pcbnew.FromMM(0.05))
    board.Add(shape)


def add_text(
    board: pcbnew.BOARD,
    text: str,
    x: float,
    y: float,
    size: float = 1.0,
    layer: int = pcbnew.F_SilkS,
) -> None:
    item = pcbnew.PCB_TEXT(board)
    item.SetText(text)
    item.SetPosition(point(x, y))
    item.SetTextSize(point(size, size))
    item.SetTextThickness(pcbnew.FromMM(max(0.15, size * 0.15)))
    item.SetLayer(layer)
    board.Add(item)


def assign(fp: pcbnew.FOOTPRINT, pad_number: int, net: pcbnew.NETINFO_ITEM) -> None:
    pad = fp.FindPadByNumber(str(pad_number))
    if pad is None:
        raise RuntimeError(f"{fp.GetReference()} has no pad {pad_number}")
    pad.SetNet(net)


def generate_board() -> None:
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(2)

    net_names = [
        "GND",
        "+3V3",
        "BCLK_IN",
        "LRCLK_IN",
        "DATA_IN",
        "BCLK_A",
        "LRCLK_A",
        "DATA_A",
        "BCLK",
        "LRCLK",
        "DATA",
    ]
    nets: dict[str, pcbnew.NETINFO_ITEM] = {}
    for name in net_names:
        net = pcbnew.NETINFO_ITEM(board, name)
        board.Add(net)
        nets[name] = net

    j1 = add_footprint(
        board,
        "Connector_PinHeader_2.54mm",
        "PinHeader_1x04_P2.54mm_Vertical",
        "J1",
        "NAD_DIGITAL_AUDIO",
        28,
        35,
    )
    r1 = add_footprint(
        board,
        "Resistor_THT",
        "R_Axial_DIN0204_L3.6mm_D1.6mm_P5.08mm_Horizontal",
        "R1",
        "1k BCLK",
        33,
        35,
    )
    r2 = add_footprint(
        board,
        "Resistor_THT",
        "R_Axial_DIN0204_L3.6mm_D1.6mm_P5.08mm_Horizontal",
        "R2",
        "1k LRCLK",
        33,
        37.54,
    )
    r3 = add_footprint(
        board,
        "Resistor_THT",
        "R_Axial_DIN0204_L3.6mm_D1.6mm_P5.08mm_Horizontal",
        "R3",
        "1k DATA",
        33,
        40.08,
    )
    u1 = add_footprint(
        board,
        "Package_DIP",
        "DIP-20_W7.62mm",
        "U1",
        "SN74LVC245AN",
        48,
        32,
    )
    c1 = add_footprint(
        board,
        "Capacitor_THT",
        "C_Disc_D5.0mm_W2.5mm_P2.50mm",
        "C1",
        "100nF",
        58.5,
        32,
        90,
    )
    j3 = add_footprint(
        board,
        "Connector_PinSocket_2.54mm",
        "PinSocket_1x18_P2.54mm_Vertical",
        "J3",
        "HELTEC_LEFT",
        75,
        73.18,
        180,
    )
    j2 = add_footprint(
        board,
        "Connector_PinSocket_2.54mm",
        "PinSocket_1x18_P2.54mm_Vertical",
        "J2",
        "HELTEC_RIGHT",
        97.86,
        73.18,
        180,
    )

    for reference, x, y in (
        ("H1", 24, 24),
        ("H2", 108, 24),
        ("H3", 24, 76),
        ("H4", 108, 76),
    ):
        mounting_hole = add_footprint(
            board,
            "MountingHole",
            "MountingHole_3.2mm_M3",
            reference,
            "M3",
            x,
            y,
        )
        mounting_hole.Reference().SetVisible(False)

    for pad_number, name in enumerate(("BCLK_IN", "LRCLK_IN", "DATA_IN", "GND"), 1):
        assign(j1, pad_number, nets[name])

    for resistor, source, buffered in (
        (r1, "BCLK_IN", "BCLK_A"),
        (r2, "LRCLK_IN", "LRCLK_A"),
        (r3, "DATA_IN", "DATA_A"),
    ):
        assign(resistor, 1, nets[source])
        assign(resistor, 2, nets[buffered])
        resistor.Reference().SetVisible(False)

    u1_assignments = {
        1: "+3V3",
        2: "BCLK_A",
        3: "LRCLK_A",
        4: "DATA_A",
        5: "GND",
        6: "GND",
        7: "GND",
        8: "GND",
        9: "GND",
        10: "GND",
        16: "DATA",
        17: "LRCLK",
        18: "BCLK",
        19: "GND",
        20: "+3V3",
    }
    for pad_number, name in u1_assignments.items():
        assign(u1, pad_number, nets[name])

    assign(c1, 1, nets["+3V3"])
    assign(c1, 2, nets["GND"])

    for pad_number, name in ((1, "GND"), (3, "+3V3"), (15, "BCLK"), (16, "LRCLK"), (17, "DATA")):
        assign(j3, pad_number, nets[name])

    # 92 x 60 mm board outline.
    for start, end in (
        ((20, 20), (112, 20)),
        ((112, 20), (112, 80)),
        ((112, 80), (20, 80)),
        ((20, 80), (20, 20)),
    ):
        add_edge(board, start, end)

    # Input-side series paths.
    add_track(board, nets["BCLK_IN"], [(28, 35), (33, 35)])
    add_track(board, nets["LRCLK_IN"], [(28, 37.54), (33, 37.54)])
    add_track(board, nets["DATA_IN"], [(28, 40.08), (33, 40.08)])
    add_track(board, nets["BCLK_A"], [(38.08, 35), (45, 35), (45, 34.54), (48, 34.54)])
    add_track(board, nets["LRCLK_A"], [(38.08, 37.54), (45, 37.54), (45, 37.08), (48, 37.08)])
    add_track(board, nets["DATA_A"], [(38.08, 40.08), (45, 40.08), (45, 39.62), (48, 39.62)])

    # Buffered I2S outputs.  Two use the back layer to avoid crossings.
    add_track(board, nets["BCLK"], [(55.62, 37.08), (63, 37.08), (63, 37.62), (75, 37.62)])
    add_track(
        board,
        nets["LRCLK"],
        [(55.62, 39.62), (64, 39.62), (64, 29), (72, 29), (72, 35.08), (75, 35.08)],
        pcbnew.B_Cu,
    )
    add_track(
        board,
        nets["DATA"],
        [(55.62, 42.16), (66, 42.16), (66, 48), (70, 48), (70, 32.54)],
        pcbnew.B_Cu,
    )
    add_via(board, nets["DATA"], 70, 32.54)
    add_track(board, nets["DATA"], [(70, 32.54), (75, 32.54)])

    # 3.3 V distribution and close decoupling.
    add_track(board, nets["+3V3"], [(75, 68.10), (80, 68.10), (80, 25), (55.62, 25), (55.62, 32)], pcbnew.B_Cu)
    add_track(board, nets["+3V3"], [(55.62, 25), (48, 25), (48, 32)], pcbnew.B_Cu)
    add_track(board, nets["+3V3"], [(55.62, 32), (58.5, 32)])

    # Bottom-layer ground plane.
    zone = pcbnew.ZONE(board)
    zone.SetLayer(pcbnew.B_Cu)
    zone.SetNet(nets["GND"])
    zone.SetLocalClearance(pcbnew.FromMM(0.3))
    zone.SetMinThickness(pcbnew.FromMM(0.25))
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in ((20.5, 20.5), (111.5, 20.5), (111.5, 79.5), (20.5, 79.5)):
        outline.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
    board.Add(zone)

    # Mechanical and assembly guidance.
    add_text(board, "CDtoSonos Heltec V3 carrier", 63, 22.5, 1.2)
    add_text(board, "REV A - PROTOTYPE", 63, 24.5, 0.9)
    add_text(board, "NAD: 1 BCLK  2 LRCLK  3 DATA  4 DGND", 40, 27.5, 0.8)
    add_text(board, "USB-C / OLED END", 86.5, 77.5, 0.9)
    add_text(board, "ANTENNA KEEP CLEAR", 86.5, 26.5, 0.9)
    c1.Reference().SetVisible(False)
    add_text(board, "R1/R2/R3 1k", 36, 43, 0.8)
    add_text(board, "C1 100n", 60.5, 35, 0.8)

    pcbnew.SaveBoard(str(BOARD_PATH), board)


def component(
    library_id: str,
    reference: str,
    value: str,
    footprint: str,
    x: int,
    y: int,
    orientation: str = "1 0 0 -1",
) -> str:
    timestamp = f"{zlib.crc32(reference.encode('ascii')):08X}"
    return f"""$Comp
L {library_id} {reference}
U 1 1 {timestamp}
P {x} {y}
F 0 "{reference}" H {x + 200} {y + 100} 50  0000 C CNN
F 1 "{value}" H {x + 300} {y - 100} 50  0000 C CNN
F 2 "{footprint}" H {x} {y} 50  0001 C CNN
F 3 "" H {x} {y} 50  0001 C CNN
\t1    {x} {y}
\t{orientation}
$EndComp
"""


def label(name: str, x: int, y: int) -> str:
    return f"Text GLabel {x} {y} 0    50   Passive ~ 0\n{name}\n"


def generate_legacy_schematic() -> None:
    parts = [
        component(
            "Connector_Generic:Conn_01x04",
            "J1",
            "NAD_DIGITAL_AUDIO",
            "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
            1700,
            2500,
        ),
        component(
            "Device:R",
            "R1",
            "1k_BCLK",
            "Resistor_THT:R_Axial_DIN0204_L3.6mm_D1.6mm_P5.08mm_Horizontal",
            2700,
            2350,
        ),
        component(
            "Device:R",
            "R2",
            "1k_LRCLK",
            "Resistor_THT:R_Axial_DIN0204_L3.6mm_D1.6mm_P5.08mm_Horizontal",
            2700,
            2450,
        ),
        component(
            "Device:R",
            "R3",
            "1k_DATA",
            "Resistor_THT:R_Axial_DIN0204_L3.6mm_D1.6mm_P5.08mm_Horizontal",
            2700,
            2550,
        ),
        component(
            "Connector_Generic:Conn_01x20",
            "U1",
            "SN74LVC245AN",
            "Package_DIP:DIP-20_W7.62mm",
            5000,
            4000,
        ),
        component(
            "Device:C",
            "C1",
            "100nF",
            "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm",
            3000,
            3350,
        ),
        component(
            "Connector_Generic:Conn_01x18",
            "J3",
            "HELTEC_LEFT",
            "Connector_PinSocket_2.54mm:PinSocket_1x18_P2.54mm_Vertical",
            7500,
            4000,
        ),
        component(
            "Connector_Generic:Conn_01x18",
            "J2",
            "HELTEC_RIGHT",
            "Connector_PinSocket_2.54mm:PinSocket_1x18_P2.54mm_Vertical",
            9000,
            4000,
        ),
    ]

    # Generic connector pin endpoints are 200 mil left of their origin.
    annotations: list[str] = []
    j1_y = [2350, 2450, 2550, 2650]
    for net, y in zip(("BCLK_IN", "LRCLK_IN", "DATA_IN", "GND"), j1_y):
        annotations.append(label(net, 1500, y))

    for net_left, net_right, y in (
        ("BCLK_IN", "BCLK_A", 2350),
        ("LRCLK_IN", "LRCLK_A", 2450),
        ("DATA_IN", "DATA_A", 2550),
    ):
        annotations.extend((label(net_left, 2550, y), label(net_right, 2850, y)))

    u1_nets = {
        1: "+3V3",
        2: "BCLK_A",
        3: "LRCLK_A",
        4: "DATA_A",
        5: "GND",
        6: "GND",
        7: "GND",
        8: "GND",
        9: "GND",
        10: "GND",
        16: "DATA",
        17: "LRCLK",
        18: "BCLK",
        19: "GND",
        20: "+3V3",
    }
    for pin, net in u1_nets.items():
        annotations.append(label(net, 4800, 3000 + pin * 100))
    for pin in range(11, 16):
        annotations.append(f"NoConn ~ 4800 {3000 + pin * 100}\n")

    # J3 pins: 1 GND, 3 3V3, 15 GPIO4/BCLK, 16 GPIO5/LRCLK, 17 GPIO6/DATA.
    for pin, net in ((1, "GND"), (3, "+3V3"), (15, "BCLK"), (16, "LRCLK"), (17, "DATA")):
        annotations.append(label(net, 7300, 3050 + pin * 100))
    for pin in [2, *range(4, 15), 18]:
        annotations.append(f"NoConn ~ 7300 {3050 + pin * 100}\n")
    for pin in range(1, 19):
        annotations.append(f"NoConn ~ 8800 {3050 + pin * 100}\n")

    annotations.extend((label("+3V3", 2850, 3350), label("GND", 3150, 3350)))

    schematic = """EESchema Schematic File Version 4
LIBS:power
LIBS:device
LIBS:Connector_Generic
EELAYER 29 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 1 1
Title "CDtoSonos Heltec V3 carrier"
Date "2026-07-26"
Rev "A"
Comp "CDtoSonos"
Comment1 "Prototype - verify 1:1 fit before fabrication"
Comment2 "NAD PCM1732U digital audio to ESP32-S3"
$EndDescr
Text Notes 1200 1200 0    100  ~ 20
CDtoSonos Heltec V3 carrier
Text Notes 1200 1450 0    60   ~ 12
J1: 1 BCLK, 2 LRCLK, 3 DATA, 4 DGND. U1 is represented by physical DIP pin number.
""" + "".join(parts) + "".join(annotations) + "$EndSCHEMATC\n"
    LEGACY_SCH_PATH.write_text(schematic, encoding="ascii")


def new_uuid() -> str:
    return str(uuid4())


def effects(hidden: bool = False) -> Effects:
    return Effects(font=Font(height=1.27, width=1.27), hide=hidden)


def make_library_symbol(
    name: str,
    prefix: str,
    value: str,
    pin_count: int,
    pin_spacing: float,
    pin_x: float,
    body_half_width: float,
    footprint: str,
) -> Symbol:
    height = max(5.08, (pin_count - 1) * pin_spacing + 2.54)
    parent = Symbol(
        entryName=name,
        inBom=True,
        onBoard=True,
        properties=[
            Property("Reference", prefix, 0, Position(0, -height / 2 - 2.54, 0), effects()),
            Property("Value", value, 1, Position(0, height / 2 + 2.54, 0), effects()),
            Property("Footprint", footprint, 2, Position(), effects(True)),
            Property("Datasheet", "", 3, Position(), effects(True)),
        ],
    )
    unit = Symbol(
        entryName=name,
        unitId=1,
        styleId=1,
        graphicItems=[
            SyRect(
                start=Position(-body_half_width, -height / 2),
                end=Position(body_half_width, height / 2),
                stroke=Stroke(width=0.254),
                fill=Fill(type="background"),
            )
        ],
    )
    first_y = -(pin_count - 1) * pin_spacing / 2
    for pin_number in range(1, pin_count + 1):
        unit.pins.append(
            SymbolPin(
                electricalType="passive",
                graphicalStyle="line",
                position=Position(pin_x, first_y + (pin_number - 1) * pin_spacing, 0),
                length=abs(pin_x) - body_half_width,
                name=str(pin_number),
                nameEffects=effects(),
                number=str(pin_number),
                numberEffects=effects(),
            )
        )
    parent.units.append(unit)
    return parent


def make_two_pin_symbol(name: str, prefix: str, value: str, footprint: str, capacitor: bool = False) -> Symbol:
    parent = Symbol(
        entryName=name,
        inBom=True,
        onBoard=True,
        properties=[
            Property("Reference", prefix, 0, Position(0, -2.54, 0), effects()),
            Property("Value", value, 1, Position(0, 2.54, 0), effects()),
            Property("Footprint", footprint, 2, Position(), effects(True)),
            Property("Datasheet", "", 3, Position(), effects(True)),
        ],
    )
    body_width = 1.27 if capacitor else 2.54
    unit = Symbol(
        entryName=name,
        unitId=1,
        styleId=1,
        graphicItems=[
            SyRect(
                start=Position(-body_width, -1.0),
                end=Position(body_width, 1.0),
                stroke=Stroke(width=0.254),
                fill=Fill(type="none" if capacitor else "background"),
            )
        ],
        pins=[
            SymbolPin(
                electricalType="passive",
                graphicalStyle="line",
                position=Position(-5.08, 0, 0),
                length=5.08 - body_width,
                name="1",
                nameEffects=effects(),
                number="1",
                numberEffects=effects(),
            ),
            SymbolPin(
                electricalType="passive",
                graphicalStyle="line",
                position=Position(5.08, 0, 180),
                length=5.08 - body_width,
                name="2",
                nameEffects=effects(),
                number="2",
                numberEffects=effects(),
            ),
        ],
    )
    parent.units.append(unit)
    return parent


def place_symbol(
    schematic: Schematic,
    lib_symbol: Symbol,
    reference: str,
    value: str,
    footprint: str,
    x: float,
    y: float,
    pin_count: int,
    angle: float = 0,
    in_bom: bool = True,
) -> SchematicSymbol:
    symbol_uuid = new_uuid()
    pin_y_values = [pin.position.Y for unit in lib_symbol.units for pin in unit.pins]
    half_height = max([abs(value) for value in pin_y_values], default=2.54)
    instance = SchematicSymbol(
        libraryNickname=lib_symbol.libraryNickname,
        entryName=lib_symbol.entryName,
        position=Position(x, y, angle),
        unit=1,
        inBom=in_bom,
        onBoard=True,
        uuid=symbol_uuid,
        properties=[
            Property("Reference", reference, 0, Position(x, y - half_height - 5.08, 0), effects()),
            Property("Value", value, 1, Position(x, y + half_height + 5.08, 0), effects()),
            Property("Footprint", footprint, 2, Position(x, y, 0), effects(True)),
            Property("Datasheet", "", 3, Position(x, y, 0), effects(True)),
        ],
        pins={str(number): new_uuid() for number in range(1, pin_count + 1)},
    )
    schematic.schematicSymbols.append(instance)
    schematic.symbolInstances.append(
        SymbolInstance(
            path=f"/{symbol_uuid}",
            reference=reference,
            unit=1,
            value=value,
            footprint=footprint,
        )
    )
    return instance


def find_pin_position(symbol: Symbol, pin_number: int, x: float, y: float) -> tuple[float, float]:
    for unit in symbol.units:
        if unit.unitId not in (0, 1):
            continue
        for pin in unit.pins:
            if pin.number == str(pin_number):
                # KiCad schematic Y coordinates increase down the page while
                # library-symbol Y coordinates increase upward.
                return x + pin.position.X, y - pin.position.Y
    raise RuntimeError(f"{symbol.entryName} has no pin {pin_number}")


def add_global_label(schematic: Schematic, text: str, x: float, y: float, angle: float = 0) -> None:
    schematic.globalLabels.append(
        GlobalLabel(
            text=text,
            shape="passive",
            position=Position(x, y, angle),
            effects=effects(),
            uuid=new_uuid(),
        )
    )


def connect_global_label(
    schematic: Schematic,
    text: str,
    pin_x: float,
    pin_y: float,
    label_x: float,
    label_y: float | None = None,
    angle: float = 0,
) -> None:
    if label_y is None:
        label_y = pin_y
    schematic.graphicalItems.append(
        Connection(
            type="wire",
            points=[Position(pin_x, pin_y), Position(label_x, label_y)],
            stroke=Stroke(width=0),
            uuid=new_uuid(),
        )
    )
    add_global_label(schematic, text, label_x, label_y, angle)


def add_no_connect(schematic: Schematic, x: float, y: float) -> None:
    schematic.noConnects.append(NoConnect(position=Position(x, y), uuid=new_uuid()))


def generate_schematic() -> None:
    schematic = Schematic.create_new()
    schematic.uuid = new_uuid()
    schematic.titleBlock = TitleBlock(
        title="CDtoSonos Heltec V3 carrier",
        date="2026-07-26",
        revision="A",
        company="CDtoSonos",
        comments={
            1: "Prototype - verify 1:1 fit before fabrication",
            2: "NAD PCM1732U digital audio to ESP32-S3",
        },
    )

    footprints = {
        "J1": "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical",
        "U1": "Package_DIP:DIP-20_W7.62mm",
        "J3": "Connector_PinSocket_2.54mm:PinSocket_1x18_P2.54mm_Vertical",
        "J2": "Connector_PinSocket_2.54mm:PinSocket_1x18_P2.54mm_Vertical",
        "R": "Resistor_THT:R_Axial_DIN0204_L3.6mm_D1.6mm_P5.08mm_Horizontal",
        "C": "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P2.50mm",
    }
    connector_library = SymbolLib.from_file(str(KICAD_SHARE / "symbols" / "Connector_Generic.kicad_sym"))
    device_library = SymbolLib.from_file(str(KICAD_SHARE / "symbols" / "Device.kicad_sym"))
    mechanical_library = SymbolLib.from_file(str(KICAD_SHARE / "symbols" / "Mechanical.kicad_sym"))
    logic_library = SymbolLib.from_file(str(KICAD_SHARE / "symbols" / "74xx.kicad_sym"))

    def library_symbol(library: SymbolLib, name: str) -> Symbol:
        return next(symbol for symbol in library.symbols if symbol.entryName == name)

    library_symbols = {
        "J1": library_symbol(connector_library, "Conn_01x04"),
        "U1": library_symbol(logic_library, "74LS245"),
        "J18": library_symbol(connector_library, "Conn_01x18"),
        "R": library_symbol(device_library, "R"),
        "C": library_symbol(device_library, "C"),
        "H": library_symbol(mechanical_library, "MountingHole"),
    }
    for key in ("J1", "J18"):
        library_symbols[key].libraryNickname = "Connector_Generic"
        library_symbols[key].pinNamesHide = True
    library_symbols["U1"].libraryNickname = "74xx"
    # This carrier fixes DIR high and /OE low, so A1-A8 are inputs and
    # B1-B8 are outputs in this design.  Adjust the bidirectional library
    # symbol accordingly so ERC evaluates the configured use, not every
    # theoretical transceiver direction.
    for unit in library_symbols["U1"].units:
        for pin in unit.pins:
            number = int(pin.number)
            if 2 <= number <= 9:
                pin.electricalType = "input"
            elif 11 <= number <= 18:
                pin.electricalType = "output"
            elif number in (10, 20):
                pin.electricalType = "passive"
    for key in ("R", "C"):
        library_symbols[key].libraryNickname = "Device"
    library_symbols["H"].libraryNickname = "Mechanical"
    schematic.libSymbols.extend(library_symbols.values())

    # Positions are in schematic millimetres.
    place_symbol(schematic, library_symbols["J1"], "J1", "NAD_DIGITAL_AUDIO", footprints["J1"], 35.56, 60.96, 4)
    place_symbol(schematic, library_symbols["R"], "R1", "1k BCLK", footprints["R"], 48.26, 55.88, 2)
    place_symbol(schematic, library_symbols["R"], "R2", "1k LRCLK", footprints["R"], 76.20, 55.88, 2)
    place_symbol(schematic, library_symbols["R"], "R3", "1k DATA", footprints["R"], 104.14, 55.88, 2)
    place_symbol(schematic, library_symbols["U1"], "U1", "SN74LVC245AN", footprints["U1"], 121.92, 81.28, 20)
    place_symbol(schematic, library_symbols["C"], "C1", "100nF", footprints["C"], 86.36, 106.68, 2)
    place_symbol(schematic, library_symbols["J18"], "J3", "HELTEC_LEFT", footprints["J3"], 180.34, 81.28, 18)
    place_symbol(schematic, library_symbols["J18"], "J2", "HELTEC_RIGHT", footprints["J2"], 226.06, 81.28, 18)
    for reference, x, y in (
        ("H1", 180.34, 127.00),
        ("H2", 195.58, 127.00),
        ("H3", 210.82, 127.00),
        ("H4", 226.06, 127.00),
    ):
        place_symbol(
            schematic,
            library_symbols["H"],
            reference,
            "M3",
            "MountingHole:MountingHole_3.2mm_M3",
            x,
            y,
            0,
            in_bom=False,
        )

    # J1 and the three series resistors.
    for pin, net in enumerate(("BCLK_IN", "LRCLK_IN", "DATA_IN", "GND"), 1):
        x, y = find_pin_position(library_symbols["J1"], pin, 35.56, 60.96)
        connect_global_label(schematic, net, x, y, 20.32)
    for resistor_x, source, buffered in (
        (48.26, "BCLK_IN", "BCLK_A"),
        (76.20, "LRCLK_IN", "LRCLK_A"),
        (104.14, "DATA_IN", "DATA_A"),
    ):
        x1, y1 = find_pin_position(library_symbols["R"], 1, resistor_x, 55.88)
        x2, y2 = find_pin_position(library_symbols["R"], 2, resistor_x, 55.88)
        connect_global_label(schematic, source, x1, y1, resistor_x - 10.16)
        connect_global_label(schematic, buffered, x2, y2, resistor_x + 10.16, angle=180)

    # U1 is drawn as a physical pin-numbered DIP symbol.
    u1_nets = {
        1: "+3V3",
        2: "BCLK_A",
        3: "LRCLK_A",
        4: "DATA_A",
        5: "GND",
        6: "GND",
        7: "GND",
        8: "GND",
        9: "GND",
        10: "GND",
        16: "DATA",
        17: "LRCLK",
        18: "BCLK",
        19: "GND",
        20: "+3V3",
    }
    for pin in range(1, 21):
        x, y = find_pin_position(library_symbols["U1"], pin, 121.92, 81.28)
        if pin in u1_nets:
            if x > 121.92:
                connect_global_label(schematic, u1_nets[pin], x, y, 147.32, angle=180)
            else:
                connect_global_label(schematic, u1_nets[pin], x, y, 96.52)
        else:
            add_no_connect(schematic, x, y)

    # Heltec left socket: J3-15/16/17 are GPIO4/5/6.
    j3_nets = {1: "GND", 3: "+3V3", 15: "BCLK", 16: "LRCLK", 17: "DATA"}
    for pin in range(1, 19):
        x3, y3 = find_pin_position(library_symbols["J18"], pin, 180.34, 81.28)
        x2, y2 = find_pin_position(library_symbols["J18"], pin, 226.06, 81.28)
        if pin in j3_nets:
            connect_global_label(schematic, j3_nets[pin], x3, y3, 162.56)
        else:
            add_no_connect(schematic, x3, y3)
        add_no_connect(schematic, x2, y2)

    x1, y1 = find_pin_position(library_symbols["C"], 1, 86.36, 106.68)
    x2, y2 = find_pin_position(library_symbols["C"], 2, 86.36, 106.68)
    connect_global_label(schematic, "+3V3", x1, y1, 76.20)
    connect_global_label(schematic, "GND", x2, y2, 96.52, angle=180)
    schematic.to_file(str(SCHEMATIC_PATH), encoding="utf-8")

    SYMBOL_TABLE_PATH.write_text(
        """(sym_lib_table
  (lib (name "Connector_Generic")(type "KiCad")(uri "${KICAD10_SYMBOL_DIR}/Connector_Generic.kicad_sym")(options "")(descr ""))
  (lib (name "Device")(type "KiCad")(uri "${KICAD10_SYMBOL_DIR}/Device.kicad_sym")(options "")(descr ""))
  (lib (name "Mechanical")(type "KiCad")(uri "${KICAD10_SYMBOL_DIR}/Mechanical.kicad_sym")(options "")(descr ""))
  (lib (name "74xx")(type "KiCad")(uri "${KICAD10_SYMBOL_DIR}/74xx.kicad_sym")(options "")(descr ""))
)
""",
        encoding="utf-8",
    )
    FOOTPRINT_TABLE_PATH.write_text(
        """(fp_lib_table
  (lib (name "Connector_PinHeader_2.54mm")(type "KiCad")(uri "${KICAD10_FOOTPRINT_DIR}/Connector_PinHeader_2.54mm.pretty")(options "")(descr ""))
  (lib (name "Connector_PinSocket_2.54mm")(type "KiCad")(uri "${KICAD10_FOOTPRINT_DIR}/Connector_PinSocket_2.54mm.pretty")(options "")(descr ""))
  (lib (name "Resistor_THT")(type "KiCad")(uri "${KICAD10_FOOTPRINT_DIR}/Resistor_THT.pretty")(options "")(descr ""))
  (lib (name "Capacitor_THT")(type "KiCad")(uri "${KICAD10_FOOTPRINT_DIR}/Capacitor_THT.pretty")(options "")(descr ""))
  (lib (name "Package_DIP")(type "KiCad")(uri "${KICAD10_FOOTPRINT_DIR}/Package_DIP.pretty")(options "")(descr ""))
  (lib (name "MountingHole")(type "KiCad")(uri "${KICAD10_FOOTPRINT_DIR}/MountingHole.pretty")(options "")(descr ""))
)
""",
        encoding="utf-8",
    )


if __name__ == "__main__":
    generate_board()
    generate_schematic()
    print(f"Generated {BOARD_PATH.name}")
    print(f"Generated {SCHEMATIC_PATH.name}")
