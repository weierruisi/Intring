import argparse
import asyncio
import csv
import os
import sys
import time

from bleak import BleakClient, BleakScanner


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATASET_DIR = os.path.join(BASE_DIR, "dataset")

# Nordic UART Service (NUS) UUIDs
UART_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
UART_TX_CHAR_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # Notify


class DataCollector:
    def __init__(self, label, duration_sec=None):
        self.label = label
        self.duration_sec = duration_sec
        self.data_buffer = []
        self.collecting = False
        self.start_time = 0
        self.sample_count = 0

        if not os.path.exists(DATASET_DIR):
            os.makedirs(DATASET_DIR)

        self.filename = os.path.join(DATASET_DIR, f"{label}_{int(time.time())}.csv")

    def notification_handler(self, sender, data):
        del sender
        if not self.collecting:
            return

        try:
            text = data.decode("utf-8").strip()
            lines = text.split("\n")
            for line in lines:
                line = line.strip()
                if not line:
                    continue

                parts = line.split(",")
                # Expected format: ax, ay, az, gx, gy, gz
                if len(parts) >= 6:
                    try:
                        values = [float(x) for x in parts[:6]]
                        self.data_buffer.append(values)
                        self.sample_count += 1

                        if self.sample_count % 10 == 0:
                            sys.stdout.write(f"\rCollected {self.sample_count} samples...")
                            sys.stdout.flush()
                    except ValueError:
                        pass
        except Exception as e:
            print(f"Error parsing data: {e}")

    async def run(self, device):
        print(f"Connecting to {device.name} ({device.address})...")
        async with BleakClient(device) as client:
            print(f"Connected: {client.is_connected}")
            await client.start_notify(UART_TX_CHAR_UUID, self.notification_handler)

            print(f"Ready to collect data for label: '{self.label}'")
            input("Press Enter to start recording...")

            print("Recording started...")
            self.collecting = True
            self.start_time = time.time()

            while True:
                if self.duration_sec and (time.time() - self.start_time > self.duration_sec):
                    break
                await asyncio.sleep(0.1)

            self.collecting = False
            await client.stop_notify(UART_TX_CHAR_UUID)
            print(f"\nRecording finished. Total samples: {len(self.data_buffer)}")

            self.save_data()

    def save_data(self):
        if self.data_buffer:
            header = ["ax", "ay", "az", "gx", "gy", "gz"]
            with open(self.filename, "w", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(header)
                writer.writerows(self.data_buffer)
            print(f"Data saved to {self.filename}")
        else:
            print("No data collected.")


async def scan_and_select():
    print("Scanning for BLE devices...")
    devices = await BleakScanner.discover()
    if not devices:
        print("No BLE devices found.")
        return None

    for i, d in enumerate(devices):
        print(f"{i}: {d.name} ({d.address})")

    while True:
        try:
            selection = input("Select device index: ")
            idx = int(selection)
            if 0 <= idx < len(devices):
                return devices[idx]
        except ValueError:
            pass
        print("Invalid selection.")


async def main():
    parser = argparse.ArgumentParser(description="BLE IMU Data Collection")
    parser.add_argument("--label", help="Label for the gesture data")
    parser.add_argument("--duration", type=float, default=2.0, help="Duration to record in seconds")
    args = parser.parse_args()

    label = args.label
    if not label:
        label = input("Enter gesture label (e.g., wave, punch): ").strip()
        if not label:
            print("Label is required!")
            return

    device = await scan_and_select()
    if device:
        collector = DataCollector(label, args.duration)
        await collector.run(device)


if __name__ == "__main__":
    asyncio.run(main())
