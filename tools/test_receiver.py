import socket
import struct
import sys
import time
import math

# Usage: python test_receiver.py

# Header:
# magic (4), startRate (4), channels (2), numSamples (4), timestamp (8), sequence (4)
# total = 26 bytes

HEADER_FORMAT = '<IIHIQi'
HEADER_SIZE = 26
MAGIC = 0x4D324730 

def main():
    # port aus argumenten oder default
    port = 12345
    if len(sys.argv) > 1:
        port = int(sys.argv[1])
    
    # socket erstellen
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', port))
    
    print("Listening on " + str(port))
    
    count = 0
    start = time.time()
    
    while True:
        try:
            data, addr = sock.recvfrom(65535)
            
            if len(data) < HEADER_SIZE:
                continue
                
            # header lesen
            header = struct.unpack(HEADER_FORMAT, data[:HEADER_SIZE])
            magic = header[0]
            
            if magic != MAGIC:
                print("Wrong magic number!")
                continue
            
            count += 1
            
            # audio daten sind nach dem header
            audio = data[HEADER_SIZE:]
            
            # peak berechnen für anzeige
            # wir konvertieren bytes zu floats
            num_floats = int(len(audio) / 4)
            floats = struct.unpack('<' + str(num_floats) + 'f', audio)
            
            # max wert finden
            max_val = 0.0
            for f in floats:
                if abs(f) > max_val:
                    max_val = abs(f)
                    
            # db berechnen (ungefähr)
            db = -60
            if max_val > 0:
                db = 20 * math.log10(max_val)
                
            # printen
            if count % 10 == 0:
                print("Packet " + str(count) + " | Level: " + str(round(db, 2)) + " dB")

        except KeyboardInterrupt:
            print("Stopped.")
            break
        except Exception as e:
            print("Error: " + str(e))

if __name__ == '__main__':
    main()
