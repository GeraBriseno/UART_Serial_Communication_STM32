import serial
import time


ser = serial.Serial()
ser.baudrate = 115200
ser.port = 'COM3'

ser.open()

if(ser.is_open):
    print("Port:", ser.port, "opened succesfully")

ser.write(b'x')

time.sleep(.1)

ser.write(b'on')

time.sleep(.1)

ser.write(b'x')

time.sleep(2)

ser.write(b'off')

time.sleep(.1)

ser.write(b'x')

ser.close()
if(not ser.is_open):
    print("Port:", ser.port, "closed succesfully")