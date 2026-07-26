#!/usr/bin/env python3
"""Render deterministic PNG previews from the generated STL parts."""

from __future__ import annotations

import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import trimesh
from mpl_toolkits.mplot3d.art3d import Poly3DCollection


ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / "output"
PREVIEWS = ROOT / "previews"
OUTER_Y = 76.0
BASE_HEIGHT = 24.0
LID_TOP = 2.4


def load(name: str) -> trimesh.Trimesh:
    mesh = trimesh.load_mesh(OUTPUT / name, force="mesh")
    if not isinstance(mesh, trimesh.Trimesh):
        raise TypeError(f"{name} did not load as one mesh")
    return mesh


def transform_lid_to_assembly(lid: trimesh.Trimesh) -> trimesh.Trimesh:
    lid = lid.copy()
    lid.apply_translation((0, -OUTER_Y, 0))
    lid.apply_transform(
        trimesh.transformations.rotation_matrix(np.pi, (1, 0, 0))
    )
    lid.apply_translation((0, 0, BASE_HEIGHT + LID_TOP))
    return lid


def pcb_placeholder() -> trimesh.Trimesh:
    pcb = trimesh.creation.box(extents=(92, 60, 1.6))
    pcb.apply_translation((54, 38, 7.2))
    return pcb


def heltec_placeholder() -> trimesh.Trimesh:
    heltec = trimesh.creation.box(extents=(25.5, 50.2, 10.2))
    heltec.apply_translation((74.45, 38.0, 16.1))
    return heltec


def oled_placeholder() -> trimesh.Trimesh:
    oled = trimesh.creation.box(extents=(24.5, 12.0, 0.6))
    oled.apply_translation((74.4, 38.0, 21.5))
    return oled


def connector_plate_assembly() -> trimesh.Trimesh:
    plate = load("cdtosonos-connector-plate-pp2010.stl")
    plate.apply_transform(
        trimesh.transformations.rotation_matrix(np.pi / 2, (1, 0, 0))
    )
    plate.apply_translation((2, 0, 2))
    return plate


def add_mesh(
    axis,
    mesh: trimesh.Trimesh,
    colour: str,
    alpha: float = 1.0,
    edge: str | None = None,
) -> None:
    triangles = mesh.vertices[mesh.faces]
    collection = Poly3DCollection(
        triangles,
        facecolor=colour,
        edgecolor=edge or colour,
        linewidth=0.05,
        alpha=alpha,
    )
    axis.add_collection3d(collection)


def frame(axis, title: str) -> None:
    axis.set_xlim(-8, 116)
    axis.set_ylim(-16, 92)
    axis.set_zlim(0, 72)
    axis.set_box_aspect((124, 108, 72))
    axis.view_init(elev=27, azim=-55)
    axis.set_axis_off()
    axis.set_title(title, fontsize=15, pad=14)


def render(exploded: bool) -> None:
    base = load("cdtosonos-case-base.stl")
    lid = transform_lid_to_assembly(load("cdtosonos-case-lid.stl"))
    connector = connector_plate_assembly()
    pcb = pcb_placeholder()
    heltec = heltec_placeholder()
    oled = oled_placeholder()

    if exploded:
        pcb.apply_translation((0, 0, 22))
        heltec.apply_translation((0, 0, 22))
        oled.apply_translation((0, 0, 22))
        lid.apply_translation((0, 0, 36))
        connector.apply_translation((0, -10, 0))

    fig = plt.figure(figsize=(12, 7), dpi=170)
    axis = fig.add_subplot(111, projection="3d")
    add_mesh(axis, base, "#303238", 0.72 if exploded else 0.16, "#22242a")
    add_mesh(axis, pcb, "#16834e", 0.96)
    add_mesh(axis, heltec, "#d9dbe1", 0.95)
    add_mesh(axis, oled, "#15191e", 1.0)
    add_mesh(axis, connector, "#d18a29", 0.98)
    add_mesh(axis, lid, "#7d838e", 0.16 if not exploded else 0.68, "#555b66")
    frame(
        axis,
        "CDtoSonos external enclosure - exploded"
        if exploded
        else "CDtoSonos external enclosure - fit check",
    )
    fig.patch.set_facecolor("#f3f1eb")
    axis.set_facecolor("#f3f1eb")
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    name = "enclosure-exploded.png" if exploded else "enclosure-fit-check.png"
    fig.savefig(PREVIEWS / name, bbox_inches="tight", pad_inches=0.15)
    plt.close(fig)


def main() -> None:
    # Ensure the printable meshes are closed before making attractive previews.
    validation = {
        "status": "pass",
        "checks": {},
        "note": "Geometric validation only; physical fit remains unverified.",
    }
    for path in sorted(OUTPUT.glob("*.stl")):
        mesh = trimesh.load_mesh(path, force="mesh")
        if not isinstance(mesh, trimesh.Trimesh) or not mesh.is_watertight:
            raise RuntimeError(f"{path.name} is not a watertight printable mesh")
        validation["checks"][path.name] = {
            "watertight": bool(mesh.is_watertight),
            "faces": int(len(mesh.faces)),
            "volume_mm3": round(float(abs(mesh.volume)), 2),
            "bounds_mm": [
                [round(float(value), 3) for value in mesh.bounds[0]],
                [round(float(value), 3) for value in mesh.bounds[1]],
            ],
        }
    (OUTPUT / "validation.json").write_text(
        json.dumps(validation, indent=2) + "\n", encoding="utf-8"
    )
    render(exploded=True)
    render(exploded=False)
    print(f"Rendered previews in {PREVIEWS}")


if __name__ == "__main__":
    main()
