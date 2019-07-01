# MouHidInputHook

MouHidInputHook enables users to filter, modify, and inject mouse input data packets into the input data stream of HID USB mouse devices without modifying the mouse device stacks.

## Projects

### MouHidInputHook

The core driver project which implements the hook interface. This project contains an example hook callback which logs each mouse input data packet in the input data stream.

### MouHidMonitor

A command line **MouHidInputHook** client which enables mouse input data packet logging.

## Input Processing Internals

### Input Class Drivers

The input class drivers, kbdclass.sys and mouclass.sys, allow hardware-independent operation of input devices by enforcing a non-standard communication protocol between device objects in an input device stack. This protocol divides the device stack into two substacks: the hardware-independent upper stack and the hardware-dependent lower stack. The lower stack transfers input data from a physical device to the upper stack via the [class service callback](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/kbdmou/nc-kbdmou-pservice_callback_routine "PSERVICE_CALLBACK_ROUTINE callback function"). The class service callback ensures that the upper stack always receives input data in a normalized format.

### Class Service Callback

The class service callback for an input device stack is established by the input class driver's **AddDevice** routine. This routine performs the following actions:

1. Creates an upper-level class filter device object.
2. Attaches the new device object to the input device stack.
3. Initializes a [CONNECT_DATA](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/kbdmou/ns-kbdmou-_connect_data "CONNECT_DATA structure") object for this class device object.
4. Sends the connect data object inside a 'connect device' request down the device stack.

The device object whose driver completes this request is the top-level device object in the lower substack. This driver stores the connect data object inside the device extension of the corresponding device object.

### Class Data Queue

A class device object contains a circular buffer of input data packets in its device extension. This buffer, referred to as the class data queue, is effectively a producer/consumer queue.

The lower substack produces input packets by invoking the class service callback when new input data is available. The class service callback copies the input data from the input buffer maintained by the lower substack device to the class data queue.

The win32k subsystem consumes input packets by reading the input device via nt!ZwReadFile. ZwReadFile issues an irp to the top of the input device stack which is ultimately handled by the IRP_MJ_READ handler of the input class driver. This handler copies input packets from the class data queue to the irp's system buffer.

### Input Processing

The following diagram depicts the input processing system for a HID USB mouse device on Windows 7 SP1 x64.

<p align="center">
    <img src="Image/mouse_input_processing_internals.png" />
</p>

#### Upper Substack

1. The upper substack read cycle begins in win32k!StartDeviceRead. This routine issues a read request for a mouse device by invoking nt!ZwReadFile:

    ```C++
    ZwReadFile(
        MouseDeviceHandle,  // Handle to the mouse device to read packets from
                            //  (referred to as the 'target mouse device')
        NULL,
        win32k!InputApc,    // Apc routine executed when the read is completed
        MouseDeviceInfo,    // Pointer to the DEVICEINFO object for the target
                            //  mouse device (see win32k!gpDeviceInfoList)
        IoStatusBlock,
        Buffer,             // MOUSE_INPUT_DATA buffer inside MouseDeviceInfo
        Length,
        &win32k!gZero,
        0);
    ```

2. nt!ZwReadFile sends an IRP_MJ_READ irp to the top of the target mouse device stack.

3. The irp is ultimately processed by the MouClass IRP_MJ_READ handler, mouclass!MouseClassRead. This routine validates the irp then invokes mouclass!MouseClassHandleRead.

    If the class data queue of the target mouse device object contains new input data packets then mouclass!MouseClassHandleRead invokes mouclass!MouseClassReadCopyData. This routine copies the new input packets from the class data queue to the irp's system buffer.

    If the class data queue does not contain new input data packets then:

    1. The irp is appended to a linked list in the device extension of the target mouse device object (referred to as the 'pending irp list').

    2. STATUS_PENDING is returned to nt!NtReadFile.

4. The win32k!InputApc routine is invoked when the irp is completed. This routine invokes win32k!ProcessMouseInput via a function pointer in the DEVICE_TEMPLATE object for the mouse device type in the DEVICE_TEMPLATE array, win32k!aDeviceTemplate. win32k!ProcessMouseInput applies movement data from the input packets to the user desktop and queues each packet to win32k!gMouseEventQueue. The raw input thread processes this queue inside win32k!RawInputThread. Finally, win32k!InputApc restarts the read cycle by invoking win32k!StartDeviceRead.

#### Lower Substack

1. The lower substack read cycle begins in mouhid!MouHid_StartRead. This routine initializes (reuses) an IRP_MJ_READ irp, sets the completion routine to mouhid!MouHid_ReadComplete, and then sends it to the next lower device object in the device stack, a HidUsb device object, via nt!IofCallDriver. The irp is routed to the IRP_MJ_READ handler defined in the HidUsb driver object, HIDCLASS!HidpMajorHandler.

2. HIDCLASS!HidpMajorHandler invokes HIDCLASS!HidpIrpMajorRead. The irp routing and processing beyond this point is outside the scope of this analysis.

3. USBPORT!USBPORT_Core_iCompleteDoneTransfer invokes nt!IopfCompleteRequest to complete the irp after new input data is read from the physical device. The irp's completion routine, mouhid!MouHid_ReadComplete, is invoked. This routine converts the input data from its hardware-dependent format, HID report, to the hardware-independent format, MOUSE_INPUT_DATA packet. The converted packets are stored in the device extension of the MouHid device object associated with the completed irp.

4. mouhid!MouHid_ReadComplete invokes the [mouse class service callback](https://docs.microsoft.com/en-us/previous-versions/ff542394%28v%3dvs.85%29 "MouseClassServiceCallback"), mouclass!MouseClassServiceCallback, via the ClassService field of the CONNECT_DATA object in the device extension of the MouHid device object associated with the completed irp:

    ```C++
    PMOUHID_DEVICE_EXTENSION DeviceExtension = MouHidDeviceObject->DeviceExtension;
    PCONNECT_DATA ConnectData = &DeviceExtension->ConnectData;
    ULONG InputDataConsumed = 0;

    ((MOUSE_SERVICE_CALLBACK_ROUTINE)ConnectData.ClassService)(
        ConnectData.ClassDeviceObject,
        DeviceExtension->InputDataStart,
        DeviceExtension->InputDataEnd,
        &InputDataConsumed);
    ```

    mouclass!MouseClassServiceCallback uses input packets from the 'InputDataStart' buffer to complete each irp in the pending irp list of the class device object. The remaining input data packets in the 'InputDataStart' buffer are copied to the class data queue of the class device object. Finally, each serviced irp is completed via nt!IofCompleteRequest. This action is directly connected to item [4] in the **Upper Substack** description above.

5. mouhid!MouHid_ReadComplete restarts the read cycle by invoking mouhid!MouHid_StartRead.

## Interface

The MouHid Hook Manager module defines an interface for registering an **MHK hook callback** and an optional **MHK notification callback**.

The **MHK hook callback** is invoked each time the MouHid driver invokes a mouse class service callback to copy mouse input data packets from a hooked MouHid device object to the class data queue of a mouse class device object. Callers can filter and modify these input packets or inject synthesized packets like in a standard mouse filter driver.

The **MHK notification callback** is invoked when a mouse related PNP event invalidates the hook environment established when the callbacks are registered.

## Implementation

The MouHid Hook Manager module hooks the **ClassService** field of the **CONNECT_DATA** object inside the device extension of each MouHid device object. This 'hook point' is marked in the 'Mouse Input Processing' image above. These hooks are installed when a caller registers an MHK hook callback, and they are uninstalled when that caller unregisters their callback.

This module registers a PnP notification callback for mouse device interface changes. If a mouse device interface event occurs while an MHK hook callback is registered then that callback is unregistered and its corresponding MHK notification callback is invoked.

## Related Projects

### MouClassInputInjection

https://github.com/changeofpace/MouClassInputInjection

MouClassInputInjection defines a user/kernel interface for injecting mouse input data packets into the input data stream of HID USB mouse devices. This project uses the **MouHid Hook Manager** to resolve the connect data objects for HID USB mouse device stacks.

## Notes

* The MouHidInputMonitor project was developed for Windows 7 SP1 x64. Support for other platforms is unknown.
* The hook strategy is PatchGuard safe.
