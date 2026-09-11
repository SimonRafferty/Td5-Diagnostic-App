# Td5-Diagnostic-App
An OBD2 Scanner for Td5 Land Rovers, Compatible with ELM327 with it's own Android App

An OBD2 Scanner has become an essential part of most vehicle owners toolkit.  However, if you own a Land Rover Td5, your options are limited & expensive!

NanoCom from BlackBox Solutions is the go-to for most people - but it is rather expensive.  If you want to re-map your engine, it's good value - but I suspect many users just want to be able to read live data, read and clear Diagnostic Trouble Codes (DTC's).  For any other vehicle, a simple ELM327 scanner and a phone app will cost peanuts - but no such, low cost, device exists for Td5 owners.  My intention with this project is to provide an option.

Along with the linked hardware (A K-Line Interface + ESP32S3), this emulates an ELM327 scanner when plugged into a Td5 Engine.  Use an app such as 'EOBD-Facile', connected via bluetooth low energy (BLE).  Unfortunately, it is not compatible with Torque which only uses Bluetooth 3 & that's not available on an ESP32S3.

Inside your scanner, you should see a Bluetooth device called "OBDII".  Connect to this.

Alternatively, install the APK in the repo - it's completely free & open source.  Unlike the other OBD2 apps, it will also display all the information unique to Td5 vehicles.  You can easily read and clear DTC fault codes too.
