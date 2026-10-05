#!/usr/bin/env python3
"""Batch FFmpeg conversion and size checks for Pico MIDI Grids sample libraries."""
import argparse
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import wave

RATE = 44100
RAM_BYTES = 128 * 1024
DISK_BYTES = 1024 * 1024
# Conservative allowance for FAT metadata, CONFIG/SELECT, and host housekeeping.
DISK_RESERVE = 32 * 1024
SAMPLE_NAME = re.compile(r"(BD|SD|HH)(0[1-8])\.WAV", re.IGNORECASE)


def positive_seconds(value):
    seconds = float(value)
    if not math.isfinite(seconds) or seconds <= 0:
        raise argparse.ArgumentTypeError("duration must be a finite positive number")
    return seconds


def verify(path):
    with wave.open(str(path), "rb") as wav:
        if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype()) != (1, 2, RATE, "NONE"):
            raise ValueError(f"{path.name}: output is not mono 16-bit PCM at {RATE} Hz")
        frames = wav.getnframes()
        if not frames or len(wav.readframes(frames)) != frames * 2:
            raise ValueError(f"{path.name}: empty or truncated PCM output")
    return frames * 2


def run(args):
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise ValueError("ffmpeg is not installed or is not on PATH")
    source = args.input.resolve()
    output = args.output.resolve()
    if not source.is_dir():
        raise ValueError(f"input folder does not exist: {source}")
    if source == output:
        raise ValueError("input and output must be different folders; originals are preserved")
    files = {}
    for path in sorted(source.iterdir()):
        if not path.is_file() or path.suffix.lower() != ".wav":
            continue
        if not SAMPLE_NAME.fullmatch(path.name):
            print(f"Skipping {path.name}: expected BD/SD/HH plus 01..08 and .WAV")
            continue
        name = path.name.upper()
        if name in files:
            raise ValueError(f"duplicate sample name ignoring case: {name}")
        files[name] = path
    if not files:
        raise ValueError("no numbered drum WAVs found in the input folder")
    if output.exists():
        if not output.is_dir():
            raise ValueError(f"output is not a folder: {output}")
        for path in output.iterdir():
            if path.suffix.lower() == ".wav":
                if not args.overwrite or path.name not in files:
                    raise ValueError(f"output contains {path.name}; use an empty folder (or --overwrite for matching filenames)")
        if not args.overwrite and any((output / name).exists() for name in files):
            raise ValueError("output files exist; choose another folder or use --overwrite")
    output.mkdir(parents=True, exist_ok=True)
    records = []
    # Stage every conversion before replacing any output WAVs.
    with tempfile.TemporaryDirectory(prefix=".convert-", dir=output) as staging:
        for name, path in files.items():
            target = Path(staging) / name
            command = [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                       "-i", str(path), "-map", "0:a:0", "-vn", "-map_metadata", "-1",
                       "-ac", "1", "-ar", str(RATE),
                       "-af", "aresample=osf=s16:dither_method=triangular",
                       "-c:a", "pcm_s16le", "-fflags", "+bitexact", "-flags:a", "+bitexact"]
            if args.max_duration is not None:
                command += ["-t", str(args.max_duration)]
            command += ["-rf64", "never", "-f", "wav", str(target)]
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise ValueError(f"{name}: FFmpeg failed:\n{result.stderr.strip()}")
            pcm = verify(target)
            records.append((name, pcm, target.stat().st_size))
            print(f"{name}: {pcm / (RATE * 2):.3f} s, {pcm:,} PCM bytes, {target.stat().st_size:,} file bytes")
        for name, _, _ in records:
            (Path(staging) / name).replace(output / name)
    largest = {}
    for part in ("BD", "SD", "HH"):
        choices = [record for record in records if record[0].startswith(part)]
        if choices:
            largest[part] = max(choices, key=lambda record: record[1])
        else:
            print(f"NOTE: no {part} files; RAM checks cannot include existing {part} sounds on your drive")
    worst_ram = sum(record[1] for record in largest.values())
    allocated = sum(((record[2] + 511) // 512) * 512 for record in records)
    ram_ok = worst_ram <= RAM_BYTES
    disk_ok = allocated + DISK_RESERVE <= DISK_BYTES
    print(f"\nConverted {len(records)} samples to {output}")
    print("Largest per drum: " + ", ".join(f"{r[0]} ({r[1]:,} PCM bytes)" for r in largest.values()))
    print(f"Worst selected combination: {worst_ram:,}/{RAM_BYTES:,} RAM bytes — {'PASS' if ram_ok else 'TOO LARGE'}")
    print(f"Library: {allocated:,} bytes rounded to 512-byte clusters + {DISK_RESERVE:,} reserve / {DISK_BYTES:,} — {'PASS' if disk_ok else 'TOO LARGE'}")
    if not ram_ok:
        print("Some combinations will not load. Trim longer sounds, or rerun with --max-duration 0.49 to cap each sound at 490 ms.")
    if not disk_ok:
        print("The complete library may not fit. Use fewer or shorter sounds; check free space on the actual drive.")
    print("Copy the WAVs to the Pico drive root, replace their matching files, then safely eject. CONFIG.TXT and SELECT.TXT are preserved separately.")
    return 0 if ram_ok and disk_ok else 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="folder containing BD01..08.WAV, SD01..08.WAV, HH01..08.WAV")
    parser.add_argument("output", type=Path, nargs="?", default=Path("samples-converted"), help="separate output folder (default: ./samples-converted)")
    parser.add_argument("--overwrite", action="store_true", help="replace matching WAVs in the output folder; never alters input")
    parser.add_argument("--max-duration", type=positive_seconds, help="explicitly trim each sound to this many seconds; default preserves full length")
    args = parser.parse_args()
    try:
        return run(args)
    except (ValueError, OSError, wave.Error) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
