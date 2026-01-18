#!/usr/bin/env python3
import serial
import time

port = '/dev/tty.usbmodem01234567891'
ser = serial.Serial(port, 115200, timeout=1)
time.sleep(2)
ser.reset_input_buffer()

print('=== Test: device --agx --power off ===')
ser.write(b'device --agx --power off\r\n')
time.sleep(3)
while ser.in_waiting:
    print(ser.read(ser.in_waiting).decode('utf-8', errors='replace'), end='', flush=True)

print('\n\n=== Test: device --agx --power on ===')
ser.write(b'device --agx --power on\r\n')
time.sleep(3)
while ser.in_waiting:
    print(ser.read(ser.in_waiting).decode('utf-8', errors='replace'), end='', flush=True)

print('\n\n=== Test: device --agx --status ===')
ser.write(b'device --agx --status\r\n')
time.sleep(2)
while ser.in_waiting:
    print(ser.read(ser.in_waiting).decode('utf-8', errors='replace'), end='', flush=True)

ser.close()
print('\n\nDone!')
