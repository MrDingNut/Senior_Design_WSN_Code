# **TEAM C Wireless Sensor Node Code**

## Here's what needs to be installed
- ESP-IDF v5.5
- Python

## Once you're ready to flash the ESP32
### Using the ESP-IDF PowerShell
`cd "Directory"`
"Directory" will be one of the following
 `cd ...\base_station_esp`
 
 `cd ...\node_esp`
 
## If this your first time flashing an ESP32S3 in this directory, run the following
`idf.py set-target esp32s3`

`idf.py build`

**NOTE:** If the code is changed or updated, build the project again

## To flash the ESP32
`idf.py -p COMx flash`
or
`idf.py -p COMx flash monitor`

"x" is the the COM port number the ESP is using. This is found using Device Manger on Windows (under "Ports (COM & LPT)")

Adding the "Monitor" modifier will monitor the serial port of the ESP32 once it finishes flashing 

## To run the monitoring program for the first time, run "buld.bat"
The .exe is then in the "dist" directory
The .exe can then be run as desired
