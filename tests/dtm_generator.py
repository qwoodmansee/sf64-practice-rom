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
    BUTTON_D_DOWN = 0x4000

    def __init__(self, game_id="NG2E01", output_dir="tests/dtm"):
        """Initialize generator.

        Args:
            game_id: 6-char game ID (default: NG2E01 for SF64 US)
            output_dir: Where to save generated DTM files
        """
        self.game_id = game_id.encode('ascii')
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def _pack_controller_state(self, buttons=0, stick_x=0x80, stick_y=0x80):
        """Pack a single controller input frame.

        Args:
            buttons: Bitmask of button presses
            stick_x: Analog stick X (0-255, 128 is center)
            stick_y: Analog stick Y (0-255, 128 is center)

        Returns:
            14 bytes of controller state
        """
        # Button mapping for N64 controller
        return struct.pack('<HHBB',
                          buttons,           # 2 bytes: button state
                          0,                 # 2 bytes: padding
                          stick_x,          # 1 byte: analog X
                          stick_y)          # 1 byte: analog Y

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
            # Press D-pad down (0x4000 = D_DOWN)
            frames.append(self._pack_controller_state(buttons=0x4000))
            # Release and wait a frame
            frames.append(self._pack_controller_state())

        # Press A button (0x8000 = A)
        frames.append(self._pack_controller_state(buttons=0x8000))
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
            # Write header
            f.write(self.MAGIC)
            f.write(struct.pack('<I', self.VERSION))
            f.write(self.game_id)
            f.write(b'\x00' * (4 - len(self.game_id)))  # Pad game ID to 4 bytes

            # Controller config (1 byte per controller, 4 controllers)
            f.write(bytes([self.CONTROLLER_CONFIG, 0, 0, 0]))

            # Frame count (number of input frames)
            f.write(struct.pack('<I', len(frames)))

            # Rerecord count (0 for generated)
            f.write(struct.pack('<I', 0))

            # Vis config (visualization, unused)
            f.write(struct.pack('<I', 0))

            # Write all input frames
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
