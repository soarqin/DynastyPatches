"""Summarize encounter-related fields in MP*.SCI map records."""

import argparse
from collections import Counter
from pathlib import Path


RECORD_SIZE = 0x473


def read_i32(data, offset):
    return int.from_bytes(data[offset : offset + 4], "little", signed=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("map_root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    map_root = args.map_root
    paths = {}
    for pattern in ("**/*.SCI", "**/*.sci"):
        for path in map_root.glob(pattern):
            if path.name.lower().startswith("mp"):
                paths[str(path).lower()] = path

    records = []
    parsed_count = 0
    for path in sorted(paths.values(), key=lambda item: str(item).lower()):
        data = path.read_bytes()
        if len(data) % RECORD_SIZE != 0:
            raise RuntimeError(f"unexpected MP*.SCI size: {path} ({len(data)} bytes)")

        for index in range(len(data) // RECORD_SIZE):
            parsed_count += 1
            offset = index * RECORD_SIZE
            enabled = read_i32(data, offset + 0x378)
            encounter_lookup_key = read_i32(data, offset + 0x37C)
            special_flag = data[offset + 0x1A1]
            if enabled != 0 or encounter_lookup_key != 0 or special_flag != 0:
                records.append(
                    (
                        path.relative_to(map_root),
                        index,
                        special_flag,
                        enabled,
                        encounter_lookup_key,
                    )
                )

    with args.output.open("w", encoding="utf-8") as output:
        output.write(f"MP*.SCI files: {len(paths)}\n")
        output.write(f"Parsed records: {parsed_count}\n")
        output.write(f"Encounter-related records: {len(records)}\n")
        output.write(f"Record size: {RECORD_SIZE:#x}\n\n")
        output.write("Encounter-enable values:\n")
        for value, count in Counter(record[3] for record in records).most_common():
            output.write(f"  {value}: {count}\n")
        output.write("\nEncounter lookup keys:\n")
        for value, count in Counter(record[4] for record in records).most_common():
            output.write(f"  {value}: {count}\n")
        output.write("\nRecords:\n")
        for path, index, special_flag, enabled, encounter_lookup_key in records:
            output.write(
                f"  {path} record={index} special={special_flag} "
                f"enabled={enabled} encounter_lookup_key={encounter_lookup_key}\n"
            )


if __name__ == "__main__":
    main()
