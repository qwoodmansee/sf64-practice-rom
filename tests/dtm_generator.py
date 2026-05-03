#!/usr/bin/env python3
"""Generate mupen64plus .dtm movie files for E2E testing."""

import struct
import os
from pathlib import Path


class DTMGenerator:
    """Generate DTM (mupen64plus movie) files with controller input sequences."""

    # DTM format constants
    MAGIC = b'M64\x1a'
    VERSION = 3
    CONTROLLER_CONFIG = 0x05  # Controller 1 plugged in

    # N64 controller button bits, matching include/PR/os_cont.h.
    BUTTON_A = 0x8000
    BUTTON_D_DOWN = 0x0400

    def __init__(self, game_id="NG2E01", output_dir="tests/dtm"):
        """Initialize generator.

        Args:
            game_id: 6-char game ID (default: NG2E01 for SF64 US)
            output_dir: Where to save generated DTM files
        """
        self.game_id = game_id.encode('ascii')
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def _pack_controller_state(self, buttons=0, stick_x=0, stick_y=0):
        """Pack a single controller input frame.

        Args:
            buttons: Bitmask of button presses
            stick_x: Analog stick X (-128..127, 0 is center)
            stick_y: Analog stick Y (-128..127, 0 is center)

        Returns:
            4 bytes of controller state (M64 format: 2-byte buttons big-endian, signed X, signed Y)
        """
        return struct.pack('>H', buttons) + struct.pack('bb', stick_x, stick_y)

    def generate_level_test(self, level_num, frames_per_level=600):
        """Generate a DTM that tests entering a specific level.

        Args:
            level_num: Level index (0=Corneria, 1=Meteo, etc)
            frames_per_level: Frames to wait after selecting level (default 600 = 10 sec @ 60fps)

        Returns:
            Path to generated DTM file
        """
        # Input sequence:
        # 1. Wait 5 seconds for intro (300 frames)
        # 2. Select level via D-pad down (level_num times) + A button
        # 3. Wait for level to load (frames_per_level frames)

        frames = []

        # 1. Intro/menu loading (5 seconds = 300 frames at 60fps)
        for _ in range(300):
            frames.append(self._pack_controller_state())

        # 2. Navigate to level and select
        # D-pad down N times to get to level N
        for _ in range(level_num):
            # Press D-pad down
            frames.append(self._pack_controller_state(buttons=self.BUTTON_D_DOWN))
            # Release and wait a frame
            frames.append(self._pack_controller_state())

        # Press A button
        frames.append(self._pack_controller_state(buttons=self.BUTTON_A))
        frames.append(self._pack_controller_state())  # Release

        # 3. Wait for level to load
        for _ in range(frames_per_level):
            frames.append(self._pack_controller_state())

        # Write DTM file
        return self._write_dtm(level_num, frames)

    def _write_dtm(self, level_num, frames):
        """Write DTM file to disk.

        Args:
            level_num: For naming the file
            frames: List of packed controller state bytes

        Returns:
            Path to written file
        """
        dtm_path = self.output_dir / f"test_level_{level_num}.dtm"

        with open(dtm_path, 'wb') as f:
            # Write full 1024-byte M64 header (input data starts at 0x400)
            header = bytearray(1024)
            struct.pack_into('4s', header, 0x000, self.MAGIC)        # magic "M64\x1a"
            struct.pack_into('<I', header, 0x004, self.VERSION)       # version 3
            # 0x008: UID (unix timestamp) — leave 0 for generated files
            struct.pack_into('<I', header, 0x00C, len(frames))        # VI count
            struct.pack_into('<I', header, 0x010, 0)                  # rerecord count
            struct.pack_into('B',  header, 0x014, 60)                 # fps (NTSC)
            struct.pack_into('B',  header, 0x015, 1)                  # num controllers
            struct.pack_into('<I', header, 0x018, len(frames))        # input sample count
            struct.pack_into('<H', header, 0x01C, 2)                  # start type: 2 = power-on
            struct.pack_into('<I', header, 0x020, 0x0001)             # controller flags: P1 present
            # 0x100: ROM internal name (192 bytes, null-padded)
            rom_name = b'Star Fox 64'
            struct.pack_into('192s', header, 0x100, rom_name.ljust(192, b'\x00'))
            # 0x1C4: country code ('E' = US NTSC)
            struct.pack_into('<H', header, 0x1C4, 0x45)
            f.write(bytes(header))

            # Write all input frames (4 bytes each)
            for frame in frames:
                f.write(frame)

        print(f"Generated {dtm_path} ({len(frames)} frames)")
        return dtm_path


if __name__ == '__main__':
    gen = DTMGenerator()

    # Generate cold-boot tests for levels 0, 1, 2 (Corneria, Meteo, Sector X)
    levels_to_test = [0, 1, 2]
    for level in levels_to_test:
        gen.generate_level_test(level)

    print(f"\nGenerated DTM files in {gen.output_dir}")
