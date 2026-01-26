#!/usr/bin/env python3
"""
Mix2Go Test Receiver
Receives UDP audio packets and displays information about them.
Usage: python3 test_receiver.py [port]
"""

import socket
import struct
import sys
import time
import math
from datetime import datetime

# Packet header format (must match AudioPacket.h)
# uint32_t magic (4 bytes)
# uint32_t sampleRate (4 bytes)
# uint16_t numChannels (2 bytes)
# uint32_t numSamples (4 bytes)
# uint64_t timestamp (8 bytes)
# uint32_t sequenceNumber (4 bytes)
# Total header: 26 bytes

HEADER_FORMAT = '<IIHIQi'  # Little-endian: uint32, uint32, uint16, uint32, uint64, uint32
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
MAGIC = 0x4D324730  # "M2G0"

def format_bytes(num_bytes):
    """Format bytes as human readable"""
    if num_bytes < 1024:
        return f"{num_bytes} B"
    elif num_bytes < 1024 * 1024:
        return f"{num_bytes / 1024:.1f} KB"
    else:
        return f"{num_bytes / (1024 * 1024):.2f} MB"

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 12345
    
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(('0.0.0.0', port))
    
    print(f"╔══════════════════════════════════════════════════════════════╗")
    print(f"║           Mix2Go Test Receiver - Listening on :{port:<5}         ║")
    print(f"╚══════════════════════════════════════════════════════════════╝")
    print()
    print("Waiting for packets...")
    print()
    
    packets_received = 0
    bytes_received = 0
    last_sequence = -1
    packets_lost = 0
    start_time = None
    last_print_time = time.time()
    
    # Audio stats
    peak_level = 0.0
    
    try:
        while True:
            data, addr = sock.recvfrom(65535)
            
            if start_time is None:
                start_time = time.time()
                print(f"▶ First packet from {addr[0]}:{addr[1]}")
                print()
            
            packets_received += 1
            bytes_received += len(data)
            
            if len(data) < HEADER_SIZE:
                print(f"⚠ Packet too small: {len(data)} bytes")
                continue
            
            # Parse header
            header = struct.unpack(HEADER_FORMAT, data[:HEADER_SIZE])
            magic, sample_rate, num_channels, num_samples, timestamp, sequence = header
            
            # Validate magic
            if magic != MAGIC:
                print(f"⚠ Invalid magic: 0x{magic:08X} (expected 0x{MAGIC:08X})")
                continue
            
            # Check for packet loss
            if last_sequence >= 0 and sequence != last_sequence + 1:
                lost = sequence - last_sequence - 1
                if lost > 0:
                    packets_lost += lost
            last_sequence = sequence
            
            # Parse audio data
            audio_bytes = data[HEADER_SIZE:]
            num_floats = len(audio_bytes) // 4
            
            if num_floats > 0:
                # Calculate peak level
                floats = struct.unpack(f'<{num_floats}f', audio_bytes)
                current_peak = max(abs(f) for f in floats)
                peak_level = max(peak_level * 0.95, current_peak)  # Slow decay
                
                # DEBUG: Print first packet or every 100th packet details
                if packets_received == 1 or packets_received % 100 == 0:
                     print(f"DEBUG Pkt {packets_received}: Peak={current_peak:.6f}, First 4 samples: {floats[:4]}")

            
            # Print stats every 0.5 seconds
            now = time.time()
            if now - last_print_time >= 0.5:
                elapsed = now - start_time
                rate = bytes_received / elapsed if elapsed > 0 else 0
                
                # Visual level meter
                meter_width = 30
                level_db = 20 * math.log10(peak_level + 1e-10) if peak_level > 0 else -60
                level_normalized = max(0, min(1, (level_db + 60) / 60))
                filled = int(level_normalized * meter_width)
                meter = '█' * filled + '░' * (meter_width - filled)
                
                print(f"\r┃ Pkts: {packets_received:6} ┃ Lost: {packets_lost:3} ┃ "
                      f"Rate: {format_bytes(rate):>8}/s ┃ "
                      f"Level: [{meter}] {level_db:+.1f}dB ┃", end='', flush=True)
                
                last_print_time = now
                
    except KeyboardInterrupt:
        print("\n")
        print("═" * 66)
        print("Session Summary:")
        print(f"  • Packets received: {packets_received}")
        print(f"  • Packets lost: {packets_lost}")
        print(f"  • Total data: {format_bytes(bytes_received)}")
        if start_time:
            elapsed = time.time() - start_time
            print(f"  • Duration: {elapsed:.1f} seconds")
            print(f"  • Average rate: {format_bytes(bytes_received / elapsed)}/s")
        print("═" * 66)
        
    finally:
        sock.close()

if __name__ == '__main__':
    main()
