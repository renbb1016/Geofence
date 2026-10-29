import time
from datetime import date
import serial 
import csv

coordinateFilename = "coordinates"
datetime_str = str(date.today()) + '_' + time.strftime("%H-%M-%S", time.localtime())

# def initialize():

#     ser = 0

#     while ser == 0:

#         try:

#             ser = serial.Serial('COM6', 9600, timeout=2) # Times out after 2 seconds of silence

#         except:

#             print("Busy Port: Try closing Raspberry Pi IDE(Thonny) or change \"COM\" parameter")

#     time.sleep(0.6)

#     ser.write(b"START\n")

#     return ser


def initialize():
    ser = None
    while ser is None:
        try:
            ser = serial.Serial('COM4', 115200, timeout=1, write_timeout=5)
        except:
            print("Busy Port: Closing Thonny?")
            time.sleep(1)
            
    # Clear the buffer of old error messages
    ser.reset_input_buffer()
    
    # Send the START command with a carriage return
    print("Sending START signal...")
    try:
        ser.write(b"START\r\n")
    except serial.SerialTimeoutException:
        # The Pico is connected but not reading what we send (usually old firmware still
        # running the geofence from the last run). Restarting it puts it back to waiting for START.
        print("The Pico is not reading data. Unplug it and plug it back in (or press reset), then run this again.")
        ser.close()
        raise SystemExit(1)

    return ser

def serialWrite(x, ser):
    print("Waiting for Pico to wake up...")

    # STEP 1: Wait for Pico's "listening..." prompt
    while True:
        # readline() reads a full line at a time, making it much cleaner!
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        
        if line:
            print(line)
            if "listening..." in line:
                break # The Pico is ready! Break out of this waiting loop.

    # STEP 2: Send the coordinates
    cmd = str(x) + '\r\n' 
    print(f"\n--- Pico is ready! Sending Coordinates ---")

    # Send the coordinates only once, in small chunks. The Pico only reads one line,
    # so sending them twice fills its USB buffer and write() blocks forever.
    for i in range(0, len(cmd), 64):
        ser.write(cmd[i:i+64].encode('utf-8'))
        time.sleep(0.05)
    ser.flush()

    print("--- Coordinates Sent! Waiting for sensor stream ---\n")

    # STEP 3: Listen forever for incoming GPS/IMU data
    while True:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            # If we received actual text (not just a 2-second timeout blank)
            if line: 
                print(line)
                with open(f'datalog{datetime_str}.txt', 'a') as f:
                    f.write(f'{line}\n')
                    
        except serial.SerialException:
            print("\nError: The Pico disconnected unexpectedly.")
            break

        
def get_coordinates():
    try:
        filename = open(f'{coordinateFilename}.csv', 'r')
        data = list(csv.reader(filename, delimiter = ","))
        filename.close()
    except FileNotFoundError:
        print(f"Could not find {coordinateFilename}.csv! Check your file path.")
        return ""

    new_data = []
    for row in range(len(data)):
        for col in range(len(data[row])):
            if (data[row][col] == "OUTER" or data[row][col] == "INNER"):
                new_data.append(data[row][col])
            else:
                new_data.append(str(abs(float(data[row][col]))))

    return ', '.join(new_data) 

if __name__ == '__main__':
    coordinate_data = get_coordinates() 
    if coordinate_data:
        serialWrite(coordinate_data, initialize())      