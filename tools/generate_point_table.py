"""Convert the six point-table appendices in the requirements Markdown to JSON."""

from __future__ import annotations

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "bcu_ems_modbus_requirements.md"
OUTPUT = ROOT / "qmodbus-master" / "data" / "point_table.json"


def cells(line: str) -> list[str]:
    return [item.strip() for item in line.strip().strip("|").split("|")]


def parse_range(value: str) -> tuple[int, int]:
    values = [int(item, 16) for item in re.findall(r"0x([0-9a-fA-F]+)", value)]
    if not values:
        raise ValueError(f"invalid address: {value!r}")
    return values[0], values[-1]


def parse_count(number: str, use_number: str, comment: str, start: int, end: int) -> int:
    # Detail arrays use `use_number` for the configured display count while
    # `Number` may only describe the protocol maximum (for example 512).
    if re.fullmatch(r"\s*\d+\s*", use_number):
        return int(use_number.strip())
    product = re.search(r"(\d+)\s*\*\s*(\d+)", comment)
    if product:
        return int(product.group(1)) * int(product.group(2))
    match = re.search(r"(?<![A-Za-z])(\d+)(?!\s*[-~])", number)
    if match:
        return int(match.group(1))
    return end - start + 1


def parse_scale(unit: str, definition: str) -> tuple[float, float]:
    match = re.match(r"\s*(0\.\d+)", unit)
    scale = float(match.group(1)) if match else 1.0
    offset = 0.0
    if "temperature data-40" in definition.lower():
        offset = -40.0
    return scale, offset


def block_for(index: int) -> str:
    return {
        1: "Rack Signal",
        2: "Rack Measure",
        3: "Rack Control",
        4: "Rack Diag",
        5: "Rack Detail",
        6: "Alarm parameters",
    }[index]


def type_name(value: str, key: str) -> tuple[str, bool]:
    normalized = value.strip().lower()
    reserved = key.strip().lower().startswith("reserve") or normalized in ("", "x")
    if reserved:
        return "reserved", True
    if normalized == "u16":
        return "u16", False
    if normalized in ("int16", "i16"):
        return "int16", False
    if normalized in ("u32", "uint32"):
        return "u32", False
    if normalized in ("int32", "i32"):
        return "int32", False
    return normalized, False


def parse_bits(value: str) -> list[dict]:
    match = re.search(r"Bit\s*(\d+)(?:\s*-\s*(?:Bit)?\s*(\d+))?\s*:\s*(.*)",
                      value, flags=re.IGNORECASE)
    if not match:
        return []
    first = int(match.group(1))
    last = int(match.group(2) or match.group(1))
    # The optional range capture shifts the description to group 3.
    description = match.group(3).strip()
    result = []
    for bit in range(first, last + 1):
        bit_description = description
        key = re.sub(r"[^A-Za-z0-9_]+", "_", bit_description).strip("_").lower()
        if last != first:
            key = f"{key}_{bit}"
        result.append({"bit": bit, "key": key, "description": bit_description})
    return result


def is_separator_row(row: list[str]) -> bool:
    return bool(row) and all(re.fullmatch(r":?-{3,}:?", cell.strip()) for cell in row)


def convert() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    sections = re.finditer(r"^### 5\.(\d+) .*?$([\s\S]*?)(?=^### 5\.\d+ |^## |\Z)", text, re.MULTILINE)
    points: list[dict] = []
    used_keys: dict[str, int] = {}

    for section in sections:
        index = int(section.group(1))
        block = block_for(index)
        header = None
        previous = None
        for line in section.group(2).splitlines():
            if line.startswith("| 名称 |"):
                header = cells(line)
                continue
            if header is None or not line.startswith("| "):
                continue
            row = cells(line)
            if is_separator_row(row):
                continue
            if len(row) != len(header):
                continue
            values = dict(zip(header, row))
            address_text = values.get("Addr", "")
            if not address_text:
                bits = parse_bits(values.get("Definition", ""))
                if bits and previous is not None:
                    existing = {item["bit"] for item in previous["bit_fields"]}
                    previous["bit_fields"].extend(item for item in bits if item["bit"] not in existing)
                continue

            start, end = parse_range(address_text)
            source_key = values.get("Name", "").strip()
            key = source_key or f"unnamed_{start:04x}"
            if key in used_keys:
                used_keys[key] += 1
                key = f"{key}_{used_keys[source_key]}"
            else:
                used_keys[key] = 1

            data_type, reserved = type_name(values.get("Type", ""), source_key)
            count = parse_count(values.get("Number", ""), values.get("use_number", ""),
                                values.get("Comment", ""), start, end)
            scale, offset = parse_scale(values.get("Unit", ""), values.get("Definition", ""))
            source_attribute = values.get("Attribute", "").strip().upper()
            attribute = source_attribute
            # Reserved rows in the source use blank/X attributes. Normalize them
            # to read-only for the runtime validator while retaining the source value.
            if reserved and attribute not in ("R", "R/W", "W/R"):
                attribute = "R"
            read_functions = [3, 4]
            write_functions = [6, 16] if attribute in ("R/W", "W/R") and not reserved else []
            point = {
                "display_name": values.get("名称", "").strip(),
                "key": key,
                "source_key": source_key,
                "description": values.get("Description", "").strip(),
                "definition": values.get("Definition", "").strip(),
                "comment": values.get("Comment", "").strip(),
                "data_origin": values.get("Data origin", "").strip(),
                "source_signal": values.get("ASWName", "").strip(),
                "block": block,
                "source_address": address_text,
                "address": start,
                "count": count,
                "unit": values.get("Unit", "").strip(),
                "attribute": attribute,
                "source_attribute": source_attribute,
                "type": data_type,
                "scale": scale,
                "offset": offset,
                "read_functions": read_functions,
                "write_functions": write_functions,
                "reserved": reserved,
                "bit_fields": [],
            }
            points.append(point)
            previous = point

    document = {
        "schema_version": 1,
        "source": SOURCE.name,
        "source_revision": "V1.3 / 2026-04-29",
        "address_policy": {
            "rack_control": ["0x0401-0x0800", "0x0900-0x0901"],
            "rack_control_exception": "0x0900/0x0901 are control time-sync points",
        },
        "points": points,
    }
    OUTPUT.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"generated {len(points)} points -> {OUTPUT}")


if __name__ == "__main__":
    convert()
