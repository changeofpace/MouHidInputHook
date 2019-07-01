# MouHidMonitor

MouHidMonitor is a user mode client for the **MouHidInputHook** driver which enables the logging of mouse input data packets in the input stream of HID USB mouse devices.

## Usage

1. Enable test signing on the host machine.
2. Load the MouHidInputHook driver.
3. Execute MouHidMonitor.exe.
4. Use a kernel debugger or the DbgView Sysinternals tool to read the logged packet data.
5. Press **ENTER** to terminate the MouHidMonitor session.

## Notes

* The debug configuration uses the multi-threaded debug runtime library to reduce library requirements.
