#!/usr/bin/env python3
"""Regenerate the C++ web asset header from the editable web sources."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "main" / "web"
OUTPUT = ROOT / "main" / "web_assets.hpp"


def raw_string(name: str, text: str) -> str:
    delimiter = "CDTS"
    if f"){delimiter}\"" in text:
        raise ValueError(f"{name} contains the raw-string delimiter")
    return f'inline constexpr char {name}[] = R"{delimiter}({text}){delimiter}";'


assets = (
    ("kIndex", WEB / "index.html"),
    ("kStyle", WEB / "style.css"),
    ("kApp", WEB / "app.js"),
)

sections = ["#pragma once", "", "namespace cdts::web_assets {", ""]
for symbol, path in assets:
    sections.append(raw_string(symbol, path.read_text(encoding="utf-8").rstrip() + "\n"))
    sections.append("")
sections.append("}  // namespace cdts::web_assets")
sections.append("")

OUTPUT.write_text("\n".join(sections), encoding="utf-8", newline="\n")
print(f"Generated {OUTPUT}")
