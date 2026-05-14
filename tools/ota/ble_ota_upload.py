#!/usr/bin/env python3
"""
Simple BLE OTA uploader for Inter-Ring custom OTA service.

Protocol:
  - Write 0x01 to CTRL char (0xFFF1) to start OTA
  - Write firmware chunks to DATA char (0xFFF2)
  - Write 0x02 to CTRL char (0xFFF1) to finish OTA
"""

from __future__ import annotations

import argparse
import asyncio
import os
import sys
import time
from typing import Optional

from bleak import BleakClient


DEFAULT_CTRL_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"
DEFAULT_DATA_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"

CTRL_START = bytes([0x01])
CTRL_FINISH = bytes([0x02])
CTRL_ABORT = bytes([0x03])
LOG_T0_MS = time.monotonic_ns() // 1_000_000


def format_elapsed(elapsed_s: float) -> str:
    total_s = max(0, int(elapsed_s))
    minutes = total_s // 60
    seconds = total_s % 60
    return f"{minutes} min {seconds} s"


def calc_speed_kb_s(done_bytes: int, elapsed_s: float) -> float:
    if elapsed_s <= 0:
        return 0.0
    return (done_bytes / 1024.0) / elapsed_s


def calc_eta_s(done_bytes: int, total_bytes: int, avg_speed_kb_s: float) -> float:
    if avg_speed_kb_s <= 0 or done_bytes <= 0 or total_bytes <= done_bytes:
        return 0.0
    remaining_bytes = total_bytes - done_bytes
    return (remaining_bytes / 1024.0) / avg_speed_kb_s


def log_info(message: str, base_ms: Optional[int] = None) -> None:
    anchor_ms = LOG_T0_MS if base_ms is None else base_ms
    elapsed_ms = (time.monotonic_ns() // 1_000_000) - anchor_ms
    elapsed_s = max(0.0, elapsed_ms / 1000.0)
    print(f"[INFO][{format_elapsed(elapsed_s)}] {message}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Upload .bin via custom BLE OTA service")
    parser.add_argument("--address", required=True, help="BLE MAC/address, e.g. B0:81:84:6D:25:D6")
    parser.add_argument("--bin", required=True, help="Firmware .bin path")
    parser.add_argument("--ctrl-uuid", default=DEFAULT_CTRL_UUID, help="Control characteristic UUID")
    parser.add_argument("--data-uuid", default=DEFAULT_DATA_UUID, help="Data characteristic UUID")
    parser.add_argument("--chunk-size", type=int, default=244, help="Chunk size bytes (default: 244)")
    parser.add_argument(
        "--delay-ms",
        type=float,
        default=1.0,
        help="Inter-chunk delay in ms to reduce packet loss (default: 1)",
    )
    parser.add_argument(
        "--no-response",
        action="store_true",
        help="Use write without response for data chunks (faster, less reliable)",
    )
    return parser.parse_args()


def format_percent(done: int, total: int) -> str:
    if total <= 0:
        return "0.00%"
    return f"{(done * 100.0 / total):.2f}%"


async def upload_firmware(
    address: str,
    bin_path: str,
    ctrl_uuid: str,
    data_uuid: str,
    chunk_size: int,
    delay_ms: float,
    data_with_response: bool,
) -> int:
    with open(bin_path, "rb") as f:
        fw = f.read()

    total = len(fw)
    if total == 0:
        raise ValueError("Firmware file is empty")

    log_info(f"Firmware: {bin_path}")
    log_info(f"Size: {total} bytes")
    log_info(f"Address: {address}")
    log_info(f"CTRL UUID: {ctrl_uuid}")
    log_info(f"DATA UUID: {data_uuid}")
    log_info(f"Chunk size: {chunk_size}")
    log_info(f"Data write response: {data_with_response}")

    status_queue: asyncio.Queue[bytes] = asyncio.Queue()

    def on_ctrl_notify(_: str, data: bytearray) -> None:
        b = bytes(data)
        print(f"[NOTIFY] CTRL status: {b.hex()}")
        try:
            status_queue.put_nowait(b)
        except Exception:
            pass

    async with BleakClient(address, timeout=20.0) as client:
        if not client.is_connected:
            raise RuntimeError("BLE connect failed")

        log_info("Connected")

        # Subscribe status notification if available.
        try:
            await client.start_notify(ctrl_uuid, on_ctrl_notify)
            log_info("CTRL notify enabled")
        except Exception as e:
            print(f"[WARN] CTRL notify not enabled: {e}")

        try:
            log_info("Send START (0x01)")
            await client.write_gatt_char(ctrl_uuid, CTRL_START, response=True)

            # Wait START ack (0x11). If device reports partition/config error,
            # stop immediately and do not stream firmware blindly.
            start_ok = False
            for _ in range(20):
                try:
                    status = await asyncio.wait_for(status_queue.get(), timeout=0.2)
                except asyncio.TimeoutError:
                    continue
                if status == bytes([0x11]):
                    start_ok = True
                    break
                if status in (bytes([0xE1]), bytes([0xE2])):
                    raise RuntimeError(f"START rejected by device, status={status.hex()}")
            if not start_ok:
                raise RuntimeError("No START ack (0x11) from device")

            sent = 0
            upload_start_s = time.monotonic()
            upload_start_ms = time.monotonic_ns() // 1_000_000
            prev_log_s = upload_start_s
            prev_log_sent = 0
            while sent < total:
                end = min(sent + chunk_size, total)
                chunk = fw[sent:end]
                await client.write_gatt_char(data_uuid, chunk, response=data_with_response)
                sent = end

                if sent == total or sent % (chunk_size * 20) == 0:
                    now_s = time.monotonic()
                    delta_s = now_s - prev_log_s
                    delta_bytes = sent - prev_log_sent
                    speed_kb_s = calc_speed_kb_s(delta_bytes, delta_s)
                    avg_speed_kb_s = calc_speed_kb_s(sent, now_s - upload_start_s)
                    eta_s = calc_eta_s(sent, total, avg_speed_kb_s)
                    log_info(
                        f"Progress: {sent}/{total} ({format_percent(sent, total)}), "
                        f"Speed: {speed_kb_s:.2f} kB/s, Avg: {avg_speed_kb_s:.2f} kB/s, "
                        f"ETA: {format_elapsed(eta_s)}",
                        base_ms=upload_start_ms,
                    )
                    prev_log_s = now_s
                    prev_log_sent = sent

                if delay_ms > 0:
                    await asyncio.sleep(delay_ms / 1000.0)

            log_info("Send FINISH (0x02)", base_ms=upload_start_ms)
            await client.write_gatt_char(ctrl_uuid, CTRL_FINISH, response=True)
            total_elapsed_s = time.monotonic() - upload_start_s
            avg_speed_kb_s = calc_speed_kb_s(total, total_elapsed_s)
            log_info(
                f"Upload done, device should reboot soon, "
                f"Avg speed: {avg_speed_kb_s:.2f} kB/s",
                base_ms=upload_start_ms,
            )
        except Exception:
            # Best effort abort.
            try:
                await client.write_gatt_char(ctrl_uuid, CTRL_ABORT, response=True)
                print("[WARN] Sent ABORT (0x03)")
            except Exception:
                pass
            raise
        finally:
            try:
                await client.stop_notify(ctrl_uuid)
            except Exception:
                pass

    return 0


def main() -> int:
    args = parse_args()

    if not os.path.exists(args.bin):
        print(f"[ERROR] File not found: {args.bin}")
        return 2

    if args.chunk_size <= 0 or args.chunk_size > 512:
        print("[ERROR] chunk-size must be in 1..512")
        return 2

    try:
        return asyncio.run(
            upload_firmware(
                address=args.address,
                bin_path=args.bin,
                ctrl_uuid=args.ctrl_uuid,
                data_uuid=args.data_uuid,
                chunk_size=args.chunk_size,
                delay_ms=args.delay_ms,
                data_with_response=not args.no_response,
            )
        )
    except KeyboardInterrupt:
        log_info("Interrupted by user")
        return 130
    except Exception as e:
        print(f"[ERROR] {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
