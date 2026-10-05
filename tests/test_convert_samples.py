"""Integration checks for the FFmpeg sample conversion routine."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import wave

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "convert_samples.py"


@unittest.skipUnless(shutil.which("ffmpeg"), "FFmpeg is required for conversion checks")
class ConversionTests(unittest.TestCase):
    def test_format_limits_and_preservation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "originals"
            source.mkdir()
            originals = {}
            # Two-second 24-bit stereo at 48 kHz: all three input properties need conversion.
            for name in ("bd04.wav", "SD06.WAV", "HH07.WAV"):
                path = source / name
                with wave.open(str(path), "wb") as wav:
                    wav.setnchannels(2)
                    wav.setsampwidth(3)
                    wav.setframerate(48000)
                    wav.writeframes(b"\x00\x00\x10\x00\x00\x10" * 96000)
                originals[name] = path.read_bytes()
            (source / "BD09.WAV").write_bytes(b"ignored out-of-range file")
            output = root / "converted"

            def convert(*options):
                return subprocess.run([sys.executable, str(SCRIPT), str(source), str(output), *options], capture_output=True, text=True)

            full = convert()
            self.assertEqual(full.returncode, 2, full.stderr)
            self.assertIn("Worst selected combination: 529,200/131,072 RAM bytes", full.stdout)
            self.assertIn("TOO LARGE", full.stdout)
            self.assertIn("Skipping BD09.WAV", full.stdout)
            self.assertFalse((output / "BD09.WAV").exists())
            self.assertEqual(convert().returncode, 1)  # Protect existing output by default.
            trimmed = convert("--overwrite", "--max-duration", "0.49")
            self.assertEqual(trimmed.returncode, 0, trimmed.stderr)
            self.assertIn("129,654/131,072 RAM bytes", trimmed.stdout)
            for name, before in originals.items():
                self.assertEqual((source / name).read_bytes(), before)
                with wave.open(str(output / name.upper()), "rb") as wav:
                    self.assertEqual((wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype()), (1, 2, 44100, "NONE"))
                    self.assertEqual(wav.getnframes(), 21609)
            same_folder = subprocess.run([sys.executable, str(SCRIPT), str(source), str(source), "--overwrite"], capture_output=True, text=True)
            self.assertEqual(same_folder.returncode, 1)


if __name__ == "__main__":
    unittest.main()
