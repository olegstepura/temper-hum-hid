TemperHum HID API and daemon for linux
======================================

This is my own implementation written in C. It's a daemon that can log results 
differently, has debug command line switch etc.

Reads temperature and humidity values from a TEMPerHUM HID device (1130:660c)

TEMPerHUM HID device is recognized by modern linux distributions as "Tenx
Technology, Inc. Foot Pedal/Thermometer". It's a device with a Tenx HID chip
which controls the onboard SHT1x temperature sensor. No /dev/ttyUSBx is created
for such device.

This program uses corrections to measrements which are described in original
SHT1x sensor datasheet. Supports multiple devices.

There was an eariler revision of TEMPerHUM which did not have a HID chip but
was using USB-to-serial CH341 chip instead. If you have such device, use Simon
Arlott's program instead: http://github.com/lp0/temperhum

``` bash
Usage: temper-hum-hid [OPTIONS]...

  -h, --help                Print help and exit
  -V, --version             Print version and exit
  -v, --verbose[=filename]  Print debug messages, to standard output if no
                              filename given  (default='')
  -s, --syslog              Log debug messages to syslog  (default=off)
  -l, --log=filename        Log data to log file
  -o, --out=filename        Output results to a file instead of printing it on
                              screen, can be used for creating a status file
                              which always has latest measurments
  -r, --repeat=seconds      Constantly print results, repeat every given amount
                              of seconds, devices will be reopened every 1 hour
                              in this mode, 0 for no repeat  (default='0')
  -m, --machine             Output in machine-friendly format, which is easier
                              to be parsed by bash scripts for later use in
                              monitoring tools, 4ex. Zabbix  (default=off)
Usage example:

  temper-hum-hid --log=/var/log/temper-hum-hid.log --out=/var/log/temper-hum-hid.status --repeat=60 --machine
```



Brando USB TemperHum device
---------------------------

[From brando site:][1]

USB Hygro-Thermometer

Product Code: ULIFE015100

The USB Hygro-Thermometer let you get easy to measure the indoor temperature & humidity
levels and able to capture both data into your computer.

Features:
---------

* Powered by USB
* Temperature Range: -40° ~ 120°
* Humidity Range: 0 ~ 100%
* Temperature can be captured from every second to 12 hours
* The logged data can be pasted to Word / Excel easily
* Support Windows XP / Vista / 7 (32-bit)
* Size: 59x17x7mm (approx.)
* Weight: 8g


[<img width="200" src="https://github.com/olegstepura/HID-TEMPerHUM/blob/master/photo.jpg?raw=true" />][2]

[1]: http://usb.brando.com/prod_detail.php?prod_id=00455
[2]: https://github.com/olegstepura/HID-TEMPerHUM/blob/master/photo.jpg?raw=true