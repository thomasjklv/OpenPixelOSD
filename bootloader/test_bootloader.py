# SPDX-License-Identifier: GPL-2.0-only
"""Execute the built Cortex-M bootloader against a deliberately small MMIO model.

Requires unicorn==2.1.4. This checks actual compiled packet/flash control flow,
not electrical timing, flash endurance, ECC hardware, or UART clock accuracy.
Run from the repository root: python bootloader/test_bootloader.py
"""
import struct
import subprocess
import unittest
import zlib
from collections import deque
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_LR

ROOT = Path(__file__).resolve().parents[1]
FLASH = 0x08000000
APP = 0x08002000
META = 0x08001800
END = 0x0801F000
UART = 0x40013800
FREG = 0x40022000


def frame(command, payload=b"", sequence=0x1234):
    body = struct.pack("<BHH", command, sequence, len(payload)) + payload
    return b"OPBL" + body + struct.pack("<I", zlib.crc32(body))


def application(length=520):
    return struct.pack("<II", 0x20005800, APP + 9) + bytes((n * 73) % 256 for n in range(length - 8))


class Device:
    def __init__(self, flash=None, boot_request=False):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for address, size in [(FLASH, 0x20000), (0x20000000, 0x6000), (0x1FFF7000, 0x1000),
                              (0x40000000, 0x30000), (0x48000000, 0x1000), (0xE0000000, 0x50000)]:
            self.uc.mem_map(address, size)
        self.uc.mem_write(FLASH, flash if flash is not None else b"\xff" * 0x20000)
        if flash is None:
            self.uc.mem_write(FLASH, (ROOT / "build/bootloader/OpenPixelOSD_Bootloader.bin").read_bytes())
        self.uc.mem_write(0x1FFF75E0, struct.pack("<H", 128))
        self.put(0xE0042000, 0x468)
        if boot_request:
            self.put(0x40002508, 0x3447504F)
        self.rx, self.tx = deque(), bytearray()
        self.cr, self.sr, self.cycles = 1 << 31, 0, 0
        self.erased, self.programmed = [], []
        self.fail_erase, self.fail_program = None, None
        self.reset = False
        self.stop_after_reply = False
        self.uc.hook_add(UC_HOOK_MEM_READ, self.read)
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self.write)
        stack, entry = struct.unpack("<II", self.uc.mem_read(FLASH, 8))
        self.uc.reg_write(UC_ARM_REG_SP, stack)
        self.uc.reg_write(UC_ARM_REG_PC, entry)
        self.run()

    def put(self, address, value):
        self.uc.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def read(self, uc, access, address, size, value, _):
        if address == UART + 0x1C:
            self.put(address, 0xC0 | (0x20 if self.rx else 0))
            if self.stop_after_reply and not self.rx and len(self.tx) >= 9 and len(self.tx) == 13 + struct.unpack_from("<H", self.tx, 7)[0]:
                uc.emu_stop()
        elif address == UART + 0x24:
            self.put(address, self.rx.popleft())
        elif address == FREG + 0x14:
            self.put(address, self.cr)
        elif address == FREG + 0x10:
            self.put(address, self.sr)
        elif address == 0xE0001004:
            self.cycles += 160
            self.put(address, self.cycles)

    def write(self, uc, access, address, size, value, _):
        if address == UART + 0x28:
            self.tx.append(value & 255)
        elif address == FREG + 8 and value == 0xCDEF89AB:
            self.cr &= ~(1 << 31)
        elif address == FREG + 0x10:
            self.sr &= ~value
        elif address == FREG + 0x14:
            self.cr = value
            if value & 0x10000:
                assert value & 2, "Only page erase is permitted"
                page = FLASH + ((value >> 3) & 63) * 2048
                assert META <= page < END, "Erase crossed protected flash"
                self.erased.append(page)
                if page == self.fail_erase:
                    self.sr = 0x10
                else:
                    self.uc.mem_write(page, b"\xff" * 2048)
                self.cr &= ~0x10000
        elif FLASH <= address < FLASH + 0x20000:
            assert META <= address < END and self.cr & 1, "Write outside application/metadata or without PG"
            assert not self.cr & (1 << 31), "Flash is locked"
            old = int.from_bytes(self.uc.mem_read(address, size), "little")
            assert value | old == old, "Flash was not erased"
            self.programmed.append(address)
            if address == self.fail_program:
                self.sr = 8
        elif address == 0xE000ED0C and value & 4:
            self.reset = True
            uc.emu_stop()

    def run(self, count=30000):
        self.uc.emu_start(self.uc.reg_read(UC_ARM_REG_PC) | 1, 0, count=count)

    def request(self, command, payload=b"", status=0):
        self.tx.clear()
        self.rx.extend(frame(command, payload))
        self.stop_after_reply = command != 5
        self.run(20000000 if command == 4 else 400000)
        self.stop_after_reply = False
        reply = bytes(self.tx)
        assert reply[:4] == b"OPBR", reply
        assert reply[4:7] == bytes([command | 128, 0x34, 0x12])
        assert len(reply) == 13 + struct.unpack_from("<H", reply, 7)[0]
        assert zlib.crc32(reply[4:-4]) == struct.unpack_from("<I", reply, len(reply) - 4)[0]
        assert reply[9] == status, (reply[9], status)
        return reply[10:-4]

    def begin(self, image):
        self.request(1)
        self.request(2, struct.pack("<II", len(image), zlib.crc32(image)))

    def upload(self, image):
        self.begin(image)
        for offset in range(0, len(image), 256):
            self.request(3, struct.pack("<I", offset) + image[offset:offset + 256])

    def snapshot(self):
        return bytes(self.uc.mem_read(FLASH, 0x20000))


class BootloaderTests(unittest.TestCase):
    def test_application_requires_valid_enter_token_and_crc_before_reset(self):
        device = Device()
        elf = ROOT / "build/uart/OpenPixelOSD.elf"
        symbols = subprocess.check_output(["arm-none-eabi-nm", str(elf)], text=True)
        entry = next(int(line.split()[0], 16) for line in symbols.splitlines() if line.endswith(" opg4_enter_byte"))
        device.uc.mem_write(APP, (ROOT / "build/uart/OpenPixelOSD_STM32G431_UART.bin").read_bytes())
        device.uc.mem_write(0x20000000, b"\x00" * 0x6000)
        device.uc.reg_write(UC_ARM_REG_SP, 0x20005800)
        def send(data):
            for byte in data:
                device.uc.reg_write(UC_ARM_REG_R0, byte)
                device.uc.reg_write(UC_ARM_REG_LR, 0x08001701)
                device.uc.emu_start(entry | 1, 0x08001700, count=10000)
        bad_crc = bytearray(frame(16, b"BOOTG431"))
        bad_crc[-1] ^= 1
        send(bad_crc)
        send(frame(16, b"BOOTG474"))
        self.assertFalse(device.reset)
        send(frame(16, b"BOOTG431"))
        self.assertTrue(device.reset)
        self.assertEqual(device.uc.mem_read(0x40002508, 4), struct.pack("<I", 0x3447504F))

    def test_compiled_binary_programs_and_commits_exact_bytes(self):
        device = Device()
        protected_boot = device.snapshot()[:0x1800]
        device.uc.mem_write(END, b"\xA5" * 4096)
        image = application()
        device.upload(image)
        self.assertEqual(device.erased[0], META)
        self.assertEqual(bytes(device.uc.mem_read(APP, len(image))), image)
        self.assertEqual(device.uc.mem_read(META, 8), b"\xff" * 8)
        device.request(4)
        self.assertEqual(device.uc.mem_read(META, 8), struct.pack("<II", 0x3147504F, 0xCEB8AFB0))
        self.assertEqual(device.snapshot()[:0x1800], protected_boot)
        self.assertEqual(device.uc.mem_read(END, 4096), b"\xA5" * 4096)
        device.request(5)
        self.assertTrue(device.reset)

    def test_rejects_bad_chip_before_erasing(self):
        device = Device()
        device.put(0xE0042000, 0x469)
        device.request(1, status=6)
        device.request(2, struct.pack("<II", 16, 0), status=6)
        self.assertEqual(device.erased, [])

    def test_requires_handshake_and_bounds_erase(self):
        device = Device()
        device.request(2, struct.pack("<II", 16, 0), status=3)
        device.request(1)
        for size in [0, 7, 9, END - APP + 8, 0xFFFFFFFF]:
            device.request(2, struct.pack("<II", size, 0), status=2)
        self.assertEqual(device.erased, [])

    def test_rejects_reordered_duplicate_overflow_and_unaligned_writes(self):
        device = Device()
        image = application(16)
        device.begin(image)
        for offset, data in [(8, image[:8]), (0xFFFFFFF8, image), (0, image + b"\x00" * 8), (0, image[:7])]:
            device.request(3, struct.pack("<I", offset) + data, status=2)
        device.request(3, struct.pack("<I", 0) + image)
        device.request(3, struct.pack("<I", 0) + image, status=2)
        device.request(4)

    def test_incomplete_image_cannot_commit_or_run(self):
        device = Device()
        device.begin(application())
        device.request(4, status=3)
        device.request(5, status=3)
        recovered = Device(device.snapshot())
        recovered.request(1)
        recovered.request(5, status=3)
        recovered.upload(application(16))
        recovered.request(4)
        recovered.request(5)
        self.assertTrue(recovered.reset)

    def test_bad_flash_crc_or_vectors_never_commit(self):
        for corrupt_vectors in [False, True]:
            device = Device()
            image = application(16)
            if corrupt_vectors:
                image = b"\x00" * 8 + image[8:]
            device.upload(image)
            if not corrupt_vectors:
                device.uc.mem_write(APP + 8, b"\x01")
            device.request(4, status=5)
            device.request(5, status=3)
            self.assertEqual(device.uc.mem_read(META, 8), b"\xff" * 8)

    def test_flash_failures_do_not_commit(self):
        device = Device()
        device.fail_erase = APP
        device.request(1)
        device.request(2, struct.pack("<II", 16, 0), status=4)
        device.request(3, struct.pack("<I", 0) + application(16), status=3)
        device.fail_erase = None
        device.begin(application(16))
        device.fail_program = APP
        device.request(3, struct.pack("<I", 0) + application(16), status=4)
        device.request(4, status=3)
        self.assertEqual(device.uc.mem_read(META, 8), b"\xff" * 8)

    def test_crc_corruption_and_oversized_frames_cannot_erase(self):
        device = Device()
        device.request(1)
        damaged = bytearray(frame(2, struct.pack("<II", 16, 0)))
        damaged[-1] ^= 1
        device.tx.clear()
        device.rx.extend(b"$M<\x00\x65\x65" + frame(2, b"\x00" * 261) + damaged)
        device.run(400000)
        self.assertEqual(device.erased, [])
        self.assertEqual(device.tx, b"")
        device.request(1)

    def test_committed_application_boots_after_window_unless_update_requested(self):
        device = Device()
        device.upload(application(16))
        device.request(4)
        snapshot = device.snapshot()
        for boot_request in [False, True]:
            rebooted = Device(snapshot, boot_request=boot_request)
            entered = []
            def application_entry(uc, address, size, _):
                entered.append(address)
                uc.emu_stop()
            rebooted.uc.hook_add(UC_HOOK_CODE, application_entry, begin=APP + 8, end=APP + 8)
            rebooted.cycles += 16000 * 1600
            rebooted.run()
            self.assertEqual(bool(entered), not boot_request)
            if boot_request:
                rebooted.request(1)


if __name__ == "__main__":
    unittest.main()
