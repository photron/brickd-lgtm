Tinkerforge Brick Daemon
========================

The Brick Daemon (brickd.exe) program is part of the Tinkerforge software
infrastructure. It routes Tinkerforge Protocol (TFP) messages between
Tinkerforge USB devices (Bricks) and TCP/IP sockets. This allows user programs
to use TCP/IP sockets to communicate with Bricks and their Bricklets connected
to USB without having to deal with USB themselves. The TCP/IP communication
itself is encapsulated in API bindings available for various programming
languages.

The installer registers and starts brickd.exe as a Windows Service. It runs
in the background and normally you should not see anything of it or have to
deal with it at all.

Drivers
-------

The drivers subfolder contains the bootloader driver and the Brick driver. The
installer will install both by default for Windows versions that need them. So
Windows should be able to use them automatically if you connect a Brick to USB.
Normally you should not have to install them manually.

The bootloader driver is needed for updating the firmware of a Brick on Windows
Vista and older Windows versions. In bootloader mode a Brick emulates a serial
port, the bootloader driver tells Windows how to handle this emulated port.
Windows 7 and newer versions don't need this driver. They automatically detect
the emulated serial port as a device called "GPS Camera Detect". This is okay.
Just select this serial port in Brick Viewer when updating a firmware.

The Brick driver if needed on Windows 7 and older Windows versions. Without
this driver Brick Daemon cannot find a Brick and it won't show up in Brick
Viewer. Windows 8 and newer versions does not need this driver. They are able
to automatically handle a Brick correctly on their own.

Commandline Options
-------------------

The Brick Daemon understands several commandline options, they are mostly for
debugging. You should not have to fiddle with them in the common case. Just let
the installer register and start brickd.exe as a Windows Service.

--help                       Show this help and exit
--version                    Show version number and exit
--check-config               Check config file for errors and exit
--install                    Register as a service and start it
--uninstall                  Stop service and unregister it
--console                    Force start as console application
--log-to-file [<log-file>]   Write log messages to overridable location
--debug [<filter>]           Set log level to debug and apply optional filter
--config-file <config-file>  Read <config-file> instead of default location

If brickd.exe is running as a service you can use the Services section in the
Computer Management to pass the commandline option --debug via the Start
Parameters option. The other options are only valid if brickd.exe is started
from a command prompt.

To start brickd.exe from a command prompt you should stop it running as Windows
Service first, because there can only be one brickd.exe running at the same
time. You can also start brickd.exe by a double click from the Explorer. Brick
Daemon detects this and does the right thing automatically.

Debugging
---------

By default Brick Daemon writes error, warning and info messages to a log file.
You can use the logviewer.exe tool to browse and save them.

The log file stores messages permanently, you can view past error, warning and
info messages. In addition the logviewer.exe tool also has a Live Log view
that connects to the currently running Brick Daemon and can show a full debug
log that can be used for detailed debugging.

The Brick Daemon config file allows to fine-tune which log messages are written
to the log file. You can use the --debug option to override the config file
settings and to output a full debug log.

If started from a command prompt and without the --log-to-file option log
messages are written to the command prompt window.

Config File
-----------

There is a config file named brickd.ini that allows to change the default
network bind address and port and control in more detail what messages are
written to the log output. See brickd.ini for more details.

Known Problems
--------------

Bricks connected to a Renesas/NEC USB 3.0 Controller (µPD720200) might not show
up in Brick Viewer. Even though the Brick shows up in the Device Manager okay
without problems.

This happens due to a bug in the driver (version < 2.1.16) for this controller.
The driver reports the address of each connected USB device as 0, which is an
invalid address (technical: SPDRP_ADDRESS of SetupDiGetDeviceRegistryProperty
is 0). This stops libusb from reporting any such USB devices to brickd. Thus,
brickd doesn't know about USB devices connected to Renesas/NEC USB controller.

This bug was fixed in driver version 2.1.16. So, if you are affected by this
problem you should update the driver for your Renesas/NEC USB 3.0 controller.
