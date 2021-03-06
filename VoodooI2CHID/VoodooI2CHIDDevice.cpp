//
//  VoodooI2CHIDDevice.cpp
//  VoodooI2CHID
//
//  Created by Alexandre on 25/08/2017.
//  Copyright © 2017 Alexandre Daoud. All rights reserved.
//

#include <IOKit/hid/IOHIDDevice.h>
#include "VoodooI2CHIDDevice.hpp"
#include "../../../VoodooI2C/VoodooI2C/VoodooI2CDevice/VoodooI2CDeviceNub.hpp"

#define super IOHIDDevice
OSDefineMetaClassAndStructors(VoodooI2CHIDDevice, IOHIDDevice);

bool VoodooI2CHIDDevice::init(OSDictionary* properties) {
    if (!super::init(properties))
        return false;
    awake = true;
    read_in_progress = true;
    read_in_progress_mutex = IOLockAlloc();
    ready_for_input = false;
    reset_event = false;
    hid_descriptor = reinterpret_cast<VoodooI2CHIDDeviceHIDDescriptor*>(IOMalloc(sizeof(VoodooI2CHIDDeviceHIDDescriptor)));
    memset(hid_descriptor, 0, sizeof(VoodooI2CHIDDeviceHIDDescriptor));

    buf_i2c_cnt_intr = 0;
    buf_i2c_cnt = 0;
    buf_i2c_pool_intr = (UInt8 *)IOMalloc(I2C_MAX_BUF_SIZE);
    buf_i2c_pool = (UInt8 *)IOMalloc(I2C_MAX_BUF_SIZE);
    
    return true;
}

void VoodooI2CHIDDevice::free() {
    IOFree(hid_descriptor, sizeof(VoodooI2CHIDDeviceHIDDescriptor));
    IOFree(buf_i2c_pool_intr, I2C_MAX_BUF_SIZE);
    IOFree(buf_i2c_pool, I2C_MAX_BUF_SIZE);
    if (read_in_progress_mutex) {
        IOLockFree(read_in_progress_mutex);
        read_in_progress_mutex = NULL;
    }

    super::free();
}

UInt8* VoodooI2CHIDDevice::getMallocI2CIntr(UInt16 size) {
    if ((buf_i2c_cnt_intr + size + 0x10) >= I2C_MAX_BUF_SIZE)
        buf_i2c_cnt_intr = 0;
    UInt8* retaddr = (buf_i2c_pool_intr + buf_i2c_cnt_intr);
    buf_i2c_cnt_intr += size + 0x10;
    
    return retaddr;
}

UInt8* VoodooI2CHIDDevice::getMallocI2C(UInt16 size) {
    if ((buf_i2c_cnt + size + 0x10) >= I2C_MAX_BUF_SIZE)
        buf_i2c_cnt = 0;
    UInt8* retaddr = (buf_i2c_pool + buf_i2c_cnt);
    buf_i2c_cnt += size + 0x10;
    
    return retaddr;
}

IOReturn VoodooI2CHIDDevice::getHIDDescriptor() {
    I2C_LOCK();
    VoodooI2CHIDDeviceCommand* command = (VoodooI2CHIDDeviceCommand*)getMallocI2C(sizeof(VoodooI2CHIDDeviceCommand));
    command->c.reg = hid_descriptor_register;

    if (api->writeReadI2C(command->data, 2, (UInt8*)hid_descriptor, (UInt16)sizeof(VoodooI2CHIDDeviceHIDDescriptor)) != kIOReturnSuccess) {
        I2C_UNLOCK();
        IOLog("%s::%s Request for HID descriptor failed\n", getName(), name);
        return kIOReturnIOError;
    }
    IOReturn ret = parseHIDDescriptor();
    I2C_UNLOCK();

    return ret;
}

IOReturn VoodooI2CHIDDevice::parseHIDDescriptor() {
    if (hid_descriptor->bcdVersion != 0x0100) {
        IOLog("%s::%s Incorrect BCD version %d\n", getName(), name, hid_descriptor->bcdVersion);
        return kIOReturnInvalid;
    }
    
    if (hid_descriptor->wHIDDescLength != sizeof(VoodooI2CHIDDeviceHIDDescriptor)) {
        IOLog("%s::%s Unexpected size of HID descriptor\n", getName(), name);
        return kIOReturnInvalid;
    }

    OSDictionary* property_array = OSDictionary::withCapacity(1);
    property_array->setObject("HIDDescLength", OSNumber::withNumber(hid_descriptor->wHIDDescLength, 32));
    property_array->setObject("BCDVersion", OSNumber::withNumber(hid_descriptor->bcdVersion, 32));
    property_array->setObject("ReportDescLength", OSNumber::withNumber(hid_descriptor->wReportDescLength, 32));
    property_array->setObject("ReportDescRegister", OSNumber::withNumber(hid_descriptor->wReportDescRegister, 32));
    property_array->setObject("MaxInputLength", OSNumber::withNumber(hid_descriptor->wMaxInputLength, 32));
    property_array->setObject("InputRegister", OSNumber::withNumber(hid_descriptor->wInputRegister, 32));
    property_array->setObject("MaxOutputLength", OSNumber::withNumber(hid_descriptor->wMaxOutputLength, 32));
    property_array->setObject("OutputRegister", OSNumber::withNumber(hid_descriptor->wOutputRegister, 32));
    property_array->setObject("CommandRegister", OSNumber::withNumber(hid_descriptor->wCommandRegister, 32));
    property_array->setObject("DataRegister", OSNumber::withNumber(hid_descriptor->wDataRegister, 32));
    property_array->setObject("VendorID", OSNumber::withNumber(hid_descriptor->wVendorID, 32));
    property_array->setObject("ProductID", OSNumber::withNumber(hid_descriptor->wProductID, 32));
    property_array->setObject("VersionID", OSNumber::withNumber(hid_descriptor->wVersionID, 32));

    setProperty("HIDDescriptor", property_array);

    property_array->release();

    return kIOReturnSuccess;
}

IOReturn VoodooI2CHIDDevice::getHIDDescriptorAddress() {
    UInt32 guid_1 = 0x3CDFF6F7;
    UInt32 guid_2 = 0x45554267;
    UInt32 guid_3 = 0x0AB305AD;
    UInt32 guid_4 = 0xDE38893D;
    
    OSObject *result = NULL;
    OSObject *params[4];
    char buffer[16];
    
    memcpy(buffer, &guid_1, 4);
    memcpy(buffer + 4, &guid_2, 4);
    memcpy(buffer + 8, &guid_3, 4);
    memcpy(buffer + 12, &guid_4, 4);
    
    
    params[0] = OSData::withBytes(buffer, 16);
    params[1] = OSNumber::withNumber(0x1, 8);
    params[2] = OSNumber::withNumber(0x1, 8);
    params[3] = OSNumber::withNumber((unsigned long long)0x0, 8);
    
    acpi_device->evaluateObject("_DSM", &result, params, 4);
    if (!result)
        acpi_device->evaluateObject("XDSM", &result, params, 4);
    if (!result) {
        IOLog("%s::%s Could not find suitable _DSM or XDSM method in ACPI tables\n", getName(), name);
        return kIOReturnNotFound;
    }
    
    OSNumber* number = OSDynamicCast(OSNumber, result);
    if (number) {
        setProperty("HIDDescriptorAddress", number);
        hid_descriptor_register = number->unsigned16BitValue();
    }

    if (result)
        result->release();
    
    params[0]->release();
    params[1]->release();
    params[2]->release();
    params[3]->release();
    
    if (!number) {
        IOLog("%s::%s HID descriptor register invalid\n", getName(), name);
        return kIOReturnInvalid;
    }
    
    return kIOReturnSuccess;
}

void VoodooI2CHIDDevice::getInputReport() {
    IOBufferMemoryDescriptor* buffer;
    IOReturn ret;
    int return_size;
    unsigned char* report;

    if (I2C_TRYLOCK() == false) {
        IOLog("%s::%s Skipping a HID read interrupt while other thread read/write I2C\n", getName(), name);
        goto exit;
    }
    report = getMallocI2CIntr(hid_descriptor->wMaxInputLength);
    report[0] = report[1] = 0;
    ret = api->readI2C(report, hid_descriptor->wMaxInputLength);
    if (ret != kIOReturnSuccess) {
        I2C_UNLOCK();
        goto exit;
    }

    return_size = report[0] | report[1] << 8;
    /*
     * "return_size" can be 0 when resetHIDDevice() is called; booting or waking up from sleep.
     * Since ready_for_input can still be FALSE when booting,
     * It needs to be checked before checking ready_for_input.
     */
     if (!return_size) {
        I2C_UNLOCK();
        command_gate->commandWakeup(&reset_event);
        goto exit;
    }

    if (!ready_for_input || return_size > hid_descriptor->wMaxInputLength) {
        I2C_UNLOCK();
        goto exit;
    }

    buffer = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, return_size);
    buffer->writeBytes(0, report + 2, return_size - 2);
    
    I2C_UNLOCK();
    
    ret = handleReport(buffer, kIOHIDReportTypeInput);
    if (ret != kIOReturnSuccess)
        IOLog("%s::%s Error handling input report: 0x%.8x\n", getName(), name, ret);
    
    buffer->release();

exit:
    read_in_progress = false;
    thread_terminate(current_thread());
}

IOWorkLoop* VoodooI2CHIDDevice::getWorkLoop(void) const {
    static IOWorkLoop* __work_loop = NULL;

    // Do we have a work loop already?, if so return it NOW.
    if ((vm_address_t) __work_loop >> 1)
        return __work_loop;
    
    if (OSCompareAndSwap(0, 1, reinterpret_cast<IOWorkLoop*>(&__work_loop))) {
        // Construct the workloop and set the __work_loop variable
        // to whatever the result is and return
        __work_loop = IOWorkLoop::workLoop();
    } else {
        while (reinterpret_cast<IOWorkLoop*>(__work_loop) == reinterpret_cast<IOWorkLoop*>(1)) {
            // Spin around the __work_loop variable until the
            // initialization finishes.
            thread_block(0);
        }
    }
    
    return __work_loop;
}

IOReturn VoodooI2CHIDDevice::getReport(IOMemoryDescriptor* report, IOHIDReportType reportType, IOOptionBits options) {
    if (reportType != kIOHIDReportTypeFeature && reportType != kIOHIDReportTypeInput)
        return kIOReturnBadArgument;

    UInt8 args[3];
    IOReturn ret;
    int args_len = 0;
    UInt16 read_register = hid_descriptor->wDataRegister;
    UInt8 report_id = options & 0xFF;
    UInt8 raw_report_type = (reportType == kIOHIDReportTypeFeature) ? 0x03 : 0x01;
    
    if (report_id >= 0x0F) {
        args[args_len++] = report_id;
        report_id = 0x0F;
    }

    args[args_len++] = read_register & 0xFF;
    args[args_len++] = read_register >> 8;
    
    I2C_LOCK();

    UInt8 length = sizeof(VoodooI2CHIDDeviceCommand);
    UInt8* buffer = (UInt8*) getMallocI2C(report->getLength());
    memset(buffer, 0, report->getLength());

    VoodooI2CHIDDeviceCommand* command = (VoodooI2CHIDDeviceCommand*) getMallocI2C(length + args_len);
    memset(command, 0, length + args_len);
    command->c.reg = hid_descriptor->wCommandRegister;
    command->c.opcode = 0x02;
    command->c.report_type_id = report_id | raw_report_type << 4;
    
    UInt8* raw_command = (UInt8*)command;
    
    memcpy(raw_command + length, args, args_len);
    ret = api->writeReadI2C(raw_command, length+args_len, buffer, report->getLength());
    report->writeBytes(0, buffer+2, report->getLength()-2);
    
    I2C_UNLOCK();

    return ret;
}

void VoodooI2CHIDDevice::interruptOccured(OSObject* owner, IOInterruptEventSource* src, int intCount) {
    if (read_in_progress)
        return;
    if (!awake)
        return;
    
    read_in_progress = true;

    thread_t new_thread;
    kern_return_t ret = kernel_thread_start(OSMemberFunctionCast(thread_continue_t, this, &VoodooI2CHIDDevice::getInputReport), this, &new_thread);
    if (ret != KERN_SUCCESS) {
        read_in_progress = false;
        IOLog("%s::%s Thread error while attempting to get input report\n", getName(), name);
    } else {
        thread_deallocate(new_thread);
    }
}

VoodooI2CHIDDevice* VoodooI2CHIDDevice::probe(IOService* provider, SInt32* score) {
    if (!super::probe(provider, score))
        return NULL;

    name = getMatchedName(provider);
    
    acpi_device = OSDynamicCast(IOACPIPlatformDevice, provider->getProperty("acpi-device"));
    
    if (!acpi_device) {
        IOLog("%s::%s Could not get ACPI device\n", getName(), name);
        return NULL;
    }
    
    // Sometimes an I2C HID will have power state methods, lets turn it on in case
    
    acpi_device->evaluateObject("_PS0");

    api = OSDynamicCast(VoodooI2CDeviceNub, provider);
    
    if (!api) {
        IOLog("%s::%s Could not get VoodooI2C API access\n", getName(), name);
        return NULL;
    }
    
    if (getHIDDescriptorAddress() != kIOReturnSuccess) {
        IOLog("%s::%s Could not get HID descriptor\n", getName(), name);
        return NULL;
    }

    if (getHIDDescriptor() != kIOReturnSuccess) {
        IOLog("%s::%s Could not get HID descriptor\n", getName(), name);
        return NULL;
    }
    read_in_progress = false;

    return this;
}

void VoodooI2CHIDDevice::releaseResources() {
    if (command_gate) {
        work_loop->removeEventSource(command_gate);
        command_gate->release();
        command_gate = NULL;
    }
    
    if (interrupt_simulator) {
        interrupt_simulator->disable();
        work_loop->removeEventSource(interrupt_simulator);
        interrupt_simulator->release();
        interrupt_simulator = NULL;
    }

    if (interrupt_source) {
        interrupt_source->disable();
        work_loop->removeEventSource(interrupt_source);
        interrupt_source->release();
        interrupt_source = NULL;
    }

    if (work_loop) {
        work_loop->release();
        work_loop = NULL;
    }
    
    if (acpi_device) {
        acpi_device->release();
        acpi_device = NULL;
    }
    
    if (api) {
        if (api->isOpen(this))
            api->close(this);
        api->release();
        api = NULL;
    }
}

IOReturn VoodooI2CHIDDevice::resetHIDDevice() {
    return command_gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &VoodooI2CHIDDevice::resetHIDDeviceGated));
}

IOReturn VoodooI2CHIDDevice::resetHIDDeviceGated() {
    setHIDPowerState(kVoodooI2CStateOn);
    
    IOSleep(1);

    I2C_LOCK();
    VoodooI2CHIDDeviceCommand* command = (VoodooI2CHIDDeviceCommand*) getMallocI2C(sizeof(VoodooI2CHIDDeviceCommand));
    command->c.reg = hid_descriptor->wCommandRegister;
    command->c.opcode = 0x01;
    command->c.report_type_id = 0;

    api->writeI2C(command->data, sizeof(VoodooI2CHIDDeviceCommand));
    I2C_UNLOCK();
    
    AbsoluteTime absolute_time, deadline;

    // Device is required to complete a host-initiated reset in at most 5 seconds.

    nanoseconds_to_absolutetime(5000000000, &absolute_time);
    clock_absolutetime_interval_to_deadline(absolute_time, &deadline);
    IOReturn sleep = command_gate->commandSleep(&reset_event, deadline, THREAD_ABORTSAFE);

    if (sleep == THREAD_TIMED_OUT) {
        IOLog("%s::%s Timeout waiting for device to complete host initiated reset\n", getName(), name);
        return kIOReturnTimeout;
    }

    return kIOReturnSuccess;
}

IOReturn VoodooI2CHIDDevice::setHIDPowerState(VoodooI2CState state) {
    I2C_LOCK();
    VoodooI2CHIDDeviceCommand* command = (VoodooI2CHIDDeviceCommand*) getMallocI2C(sizeof(VoodooI2CHIDDeviceCommand));
    command->c.reg = hid_descriptor->wCommandRegister;
    command->c.opcode = 0x08;
    command->c.report_type_id = state ? I2C_HID_PWR_ON : I2C_HID_PWR_SLEEP;
    
    IOReturn ret = api->writeI2C(command->data, sizeof(VoodooI2CHIDDeviceCommand));
    I2C_UNLOCK();

    return ret;
}

IOReturn VoodooI2CHIDDevice::setReport(IOMemoryDescriptor* report, IOHIDReportType reportType, IOOptionBits options) {
    if (reportType != kIOHIDReportTypeFeature && reportType != kIOHIDReportTypeOutput)
        return kIOReturnBadArgument;

    UInt16 data_register = hid_descriptor->wDataRegister;
    UInt8 raw_report_type = (reportType == kIOHIDReportTypeFeature) ? 0x03 : 0x02;
    UInt8 idx = 0;
    UInt16 size;
    UInt16 arguments_length;
    UInt8 report_id = options & 0xFF;
    UInt8* buffer = (UInt8*)IOMalloc(report->getLength());
    report->readBytes(0, buffer, report->getLength());
    
    size = 2 +
    (report_id ? 1 : 0)     /* reportID */ +
    report->getLength()     /* buf */;

    arguments_length = (report_id >= 0x0F ? 1 : 0)  /* optional third byte */ +
    2                                               /* dataRegister */ +
    size                                            /* args */;
    
    UInt8* arguments = (UInt8*)IOMalloc(arguments_length);
    memset(arguments, 0, arguments_length);
    
    if (report_id >= 0x0F) {
        arguments[idx++] = report_id;
        report_id = 0x0F;
    }
    
    arguments[idx++] = data_register & 0xFF;
    arguments[idx++] = data_register >> 8;
    
    arguments[idx++] = size & 0xFF;
    arguments[idx++] = size >> 8;
    
    if (report_id)
        arguments[idx++] = report_id;
    
    memcpy(&arguments[idx], buffer, report->getLength());
    
    I2C_LOCK();

    UInt8 length = sizeof(VoodooI2CHIDDeviceCommand);
    VoodooI2CHIDDeviceCommand* command = (VoodooI2CHIDDeviceCommand*) getMallocI2C(length + arguments_length);
    memset(command, 0, length + arguments_length);
    command->c.reg = hid_descriptor->wCommandRegister;
    command->c.opcode = 0x03;
    command->c.report_type_id = report_id | raw_report_type << 4;
    
    UInt8* raw_command = (UInt8*)command;
    
    memcpy(raw_command + length, arguments, arguments_length);
    IOReturn ret = api->writeI2C(raw_command, length+arguments_length);
    
    I2C_UNLOCK();

    IOFree(arguments, arguments_length);
    IOFree(buffer, report->getLength());

    return ret;
}

IOReturn VoodooI2CHIDDevice::setPowerState(unsigned long whichState, IOService* whatDevice) {
    if (whatDevice != this)
        return kIOReturnInvalid;
    if (whichState == 0) {
        if (awake) {
            awake = false;
            setHIDPowerState(kVoodooI2CStateOff);
            
            IOLog("%s::%s Going to sleep\n", getName(), name);
        }
    } else {
        if (!awake) {
            setHIDPowerState(kVoodooI2CStateOn);
            
            IOSleep(1);
            
            I2C_LOCK();
            VoodooI2CHIDDeviceCommand *command = (VoodooI2CHIDDeviceCommand*) getMallocI2C(sizeof(VoodooI2CHIDDeviceCommand));
            command->c.reg = hid_descriptor->wCommandRegister;
            command->c.opcode = 0x01;
            command->c.report_type_id = 0;
            
            api->writeI2C(command->data, sizeof(VoodooI2CHIDDeviceCommand));
            I2C_UNLOCK();
            
            IOLog("%s::%s Woke up\n", getName(), name);
            awake = true;
        }
    }
    return kIOPMAckImplied;
}

bool VoodooI2CHIDDevice::handleStart(IOService* provider) {
    if (!super::handleStart(provider)) {
        return false;
    }

    work_loop = getWorkLoop();
    
    if (!work_loop) {
        IOLog("%s::%s Could not get work loop\n", getName(), name);
        goto exit;
    }

    work_loop->retain();

    command_gate = IOCommandGate::commandGate(this);
    if (!command_gate || (work_loop->addEventSource(command_gate) != kIOReturnSuccess)) {
        IOLog("%s::%s Could not open command gate\n", getName(), name);
        goto exit;
    }

    acpi_device->retain();
    api->retain();

    if (!api->open(this)) {
        IOLog("%s::%s Could not open API\n", getName(), name);
        goto exit;
    }
    
    interrupt_source = IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &VoodooI2CHIDDevice::interruptOccured), api, 0);
    if (!interrupt_source) {
        IOLog("%s::%s Warning: Could not get interrupt event source, using polling instead\n", getName(), name);
        interrupt_simulator = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &VoodooI2CHIDDevice::simulateInterrupt));
        
        if (!interrupt_simulator) {
            IOLog("%s::%s Could not get timer event source\n", getName(), name);
            goto exit;
        }
        work_loop->addEventSource(interrupt_simulator);
        interrupt_simulator->setTimeoutMS(200);
    } else {
        work_loop->addEventSource(interrupt_source);
        interrupt_source->enable();
    }

    resetHIDDevice();


    PMinit();
    api->joinPMtree(this);
    registerPowerDriver(this, VoodooI2CIOPMPowerStates, kVoodooI2CIOPMNumberPowerStates);
    
    // Give the reset a bit of time so that IOHIDDevice doesnt happen to start requesting the report
    // descriptor before the driver is ready

    IOSleep(100);

    return true;
exit:
    releaseResources();
    return false;
}

bool VoodooI2CHIDDevice::start(IOService* provider) {
    if (!super::start(provider))
        return false;

    ready_for_input = true;

    setProperty("VoodooI2CServices Supported", OSBoolean::withBoolean(true));

    return true;
}

void VoodooI2CHIDDevice::stop(IOService* provider) {
    releaseResources();
    PMstop();
    super::stop(provider);
}

IOReturn VoodooI2CHIDDevice::newReportDescriptor(IOMemoryDescriptor** descriptor) const {
    if (!hid_descriptor->wReportDescLength) {
        IOLog("%s::%s Invalid report descriptor size\n", getName(), name);
        return kIOReturnDeviceError;
    }

    I2C_LOCK();

    VoodooI2CHIDDeviceCommand command;
    command.c.reg = hid_descriptor->wReportDescRegister;
    
    UInt8* buffer = reinterpret_cast<UInt8*>(IOMalloc(hid_descriptor->wReportDescLength));
    memset(buffer, 0, hid_descriptor->wReportDescLength);

    if (api->writeReadI2C(command.data, 2, buffer, hid_descriptor->wReportDescLength) != kIOReturnSuccess) {
        I2C_UNLOCK();
        IOFree(buffer, hid_descriptor->wReportDescLength);
        IOLog("%s::%s Could not get report descriptor\n", getName(), name);
        return kIOReturnIOError;
    }

    IOBufferMemoryDescriptor* report_descriptor = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task, 0, hid_descriptor->wReportDescLength);

    if (!report_descriptor) {
        I2C_UNLOCK();
        IOFree(buffer, hid_descriptor->wReportDescLength);
        IOLog("%s::%s Could not allocated buffer for report descriptor\n", getName(), name);
        return kIOReturnNoResources;
    }

    report_descriptor->writeBytes(0, buffer, hid_descriptor->wReportDescLength);
    *descriptor = report_descriptor;

    I2C_UNLOCK();

    IOFree(buffer, hid_descriptor->wReportDescLength);

    return kIOReturnSuccess;
}

OSNumber* VoodooI2CHIDDevice::newVendorIDNumber() const {
    return OSNumber::withNumber(hid_descriptor->wVendorID, 16);
}

OSNumber* VoodooI2CHIDDevice::newProductIDNumber() const {
    return OSNumber::withNumber(hid_descriptor->wProductID, 16);
}

OSNumber* VoodooI2CHIDDevice::newVersionNumber() const {
    return OSNumber::withNumber(hid_descriptor->wVersionID, 16);
}

OSString* VoodooI2CHIDDevice::newTransportString() const {
    return OSString::withCString("I2C");
}

OSString* VoodooI2CHIDDevice::newManufacturerString() const {
    return OSString::withCString("Apple");
}

void VoodooI2CHIDDevice::simulateInterrupt(OSObject* owner, IOTimerEventSource* timer) {
    interruptOccured(owner, NULL, NULL);
    interrupt_simulator->setTimeoutMS(INTERRUPT_SIMULATOR_TIMEOUT);
}
