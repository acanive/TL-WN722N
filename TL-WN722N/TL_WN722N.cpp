

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#pragma clang diagnostic ignored "-Wdocumentation"

#include <IOKit/IOKitKeys.h>
#include <libkern/OSByteOrder.h>
#include <libkern/OSKextLib.h>
#include <libkern/version.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/network/IOGatedOutputQueue.h>
#include <IOKit/usb/USB.h>

#include "TL_WN722N.hpp"
#include "TL_WN722NFirmware.hpp"
#pragma clang diagnostic pop

OSDefineMetaClassAndStructors(TL_WN722N, IOEthernetController)
OSDefineMetaClassAndStructors(HoRNDISInterface, IOEthernetInterface);
#define super IOEthernetController

bool TL_WN722N::init(OSDictionary *propTable)
{
    IOLog("TL_WN722N::init\n");
    fReadyToTransfer = false;
    
    fProbeConfigVal = 0;
    fProbeCommIfNum = 0;
    
    fCommInterface = NULL;
    fDataInterface = NULL;
    
    fInPipe = NULL;
    fOutPipe = NULL;
    
    maxOutTransferSize = 0;
    
    return(super::init(propTable));
}

void TL_WN722N::free(void)
{
    IOLog("TL_WN722N::free\n");
    super::free();
}

bool TL_WN722N::attach(IOService* provider)
{
    IOLog("TL_WN722N::attach\n");
    return(super::attach(provider));
}

void TL_WN722N::detach(IOService* provider)
{
    IOLog("TL_WN722N::detach\n");
    super::detach(provider);
}

IOService* TL_WN722N::probe(IOService *provider, SInt32 *score)
{
    IOLog("TL_WN722N::came in with a score of %d \n", *score);
    {  // Check if this is a device-based matching:
        IOUSBHostDevice *device = OSDynamicCast(IOUSBHostDevice, provider);
        if (device) {
            return probeDevice(device, score);
        }
    }
    return(super::probe(provider, score));
}

IOService *TL_WN722N::probeDevice(IOUSBHostDevice *device, SInt32 *score) {
    const DeviceDescriptor *desc = device->getDeviceDescriptor();
    IOLog("TL_WN722N::Device-based matching, probing: '%s', %d/%d/%d \n",
        device->getName(), desc->bDeviceClass, desc->bDeviceSubClass,
        desc->bDeviceProtocol);
    
    // Look through all configurations and find the one we want:
    for (int i = 0; i < desc->bNumConfigurations; i++) {
        const ConfigurationDescriptor *configDesc = device->getConfigurationDescriptor(i);
        if (configDesc == NULL) {
            IOLog("TL_WN722N::Cannot get device's configuration descriptor \n");
            return NULL;
        }
        int controlIfNum = INT16_MAX;  // Definitely invalid interface number.
        bool foundData = false;
        const InterfaceDescriptor *intDesc = NULL;
        while((intDesc = StandardUSB::getNextInterfaceDescriptor(configDesc, intDesc)) != NULL) {
            controlIfNum = intDesc->bInterfaceNumber;
            foundData = true;
            break;
        }
        if (foundData) {
            // We've found it! Save the information and return:
            fProbeConfigVal = configDesc->bConfigurationValue;
            fProbeCommIfNum = controlIfNum;
            *score += 10000;
            return this;
        }
    }

    // Did not find any interfaces we can use:
    IOLog("TL_WN722N::The device '%s' does not contain the required interfaces: it is not for us \n", device->getName());
    return NULL;
}


//
// start
// when this method is called, I have been selected as the driver for this device.
// I can still return false to allow a different driver to load
//
bool TL_WN722N::start(IOService *provider)
{
    IOLog("TL_WN722N::Starting TL_WN722N driver.\n");
    if(!super::start(provider)) {
        return false;
    }
  
    {  // Fixing the Provider class name.
        // See "INTERFACE PROLIFERATION AND PROVIDER CLASS NAME" description.
        OSObject *providerClass = provider->getProperty("IOClassNameOverride");
        if (providerClass) {
            setProperty(kIOProviderClassKey, providerClass);
        }
    }
    
    if (!openUSBInterfaces(provider)) {
        goto bailout;
    }
    
    /*if (!rndisInit()) { //TODO: THIS IS WORKING WITHOUT THIS BLOCK
        goto bailout;
    }*/
    
    // Let's create the medium tables here, to avoid doing extra
    // steps in 'enable'. Also, comments recommend creating medium tables
    // in the 'setup' stage.
    const IONetworkMedium *primaryMedium;
    if (!createMediumTables(&primaryMedium) ||
        !setCurrentMedium(primaryMedium)) {
        goto bailout;
    }
    
    /*if (!createMediumTables())    //TODO: THIS IS WORKING, UNCOMMENT IF THE ACTIVE ONE FAILS
    {
        IOLog("Unable to create medium tables.\n");
        return false;
    }*/
    
    // Looks like everything's good... publish the interface!
    if (!createNetworkInterface()) {
        goto bailout;
    }
    
    setLinkStatus(kIONetworkLinkValid);
    
    /*PMinit();     //TODO: UNCOMMENT IF BELOW DOESN'T WORK
    provider->joinPMtree(this);*/
    if (!initForPM(provider))
    {
        IOLog("TL_WN722N::Starting - Power Manager Initialization Failed\n");
        return false;
    }
        
    IOLog("TL_WN722N::start successful.\n");
        return true;

bailout:
    stop(provider);
    return false;
}

void TL_WN722N::stop(IOService *provider)
{
    IOLog("%s(%p)::stop\n", getName(), this);
    
    closeUSBInterfaces();  // Just in case - supposed to be closed by now.
    super::stop(provider);
}

// Convenience function: to retain and assign in one step:
template <class T> static inline T *retainT(T *ptr) {
    ptr->retain();
    return ptr;
}

bool TL_WN722N::createNetworkInterface() {
    IOLog("TL_WN722N::attaching and registering interface.\n");
    
    // MTU is initialized before we get here, so this is a safe time to do this.
    if (!attachInterface((IONetworkInterface **)&fNetworkInterface, true)) {
        IOLog("TL_WN722N::attachInterface failed?\n");
        return false;
    }
    IOLog("TL_WN722N::fNetworkInterface: %p.\n", fNetworkInterface);

    // The 'registerService' should be called by 'attachInterface' (with second
    // parameter set to true). No need to do it here.
    
    return true;
}

bool TL_WN722N::openUSBInterfaces(IOService *provider) {
    bool upgraded = false;
    if (fProbeConfigVal == 0) {
        // Must have been set by 'probe' before 'start' function call:
        IOLog("TL_WN722N::'fProbeConfigVal' has not been set, bailing out.\n");
        return false;
    }

    IOUSBHostDevice *device = OSDynamicCast(IOUSBHostDevice, provider);
    if (device) {
        // Set the device configuration, so we can start looking at the interfaces:
        if (device->setConfiguration(fProbeConfigVal, false) != kIOReturnSuccess) {
            IOLog("TL_WN722N::Cannot set the USB Device configuration.\n");
            return false;
        }
    } else {
        IOUSBHostInterface *iface = OSDynamicCast(IOUSBHostInterface, provider);
        if (iface == NULL) {
            IOLog("TL_WN722N::start: BUG unexpected provider class.\n");
            return false;
        }
        device = iface->getDevice();
        // Make sure it's the one we care about:
        bool match = iface->getConfigurationDescriptor()->bConfigurationValue == fProbeConfigVal
            && iface->getInterfaceDescriptor()->bInterfaceNumber == fProbeCommIfNum;
        if (!match) {
            IOLog("TL_WN722N::BUG! Did we see a different provider in probe?\n");
            return false;
        }
    }

    {  // Now, find the interfaces:
        OSIterator *iterator = device->getChildIterator(gIOServicePlane);
        OSObject *obj = NULL;
        while(iterator != NULL && (obj = iterator->getNextObject()) != NULL) {
            IOUSBHostInterface *iface = OSDynamicCast(IOUSBHostInterface, obj);
            if (iface == NULL) {
                continue;
            }
            if (iface->getConfigurationDescriptor()->bConfigurationValue !=
                    fProbeConfigVal) {
                continue;
            }
            const InterfaceDescriptor *desc = iface->getInterfaceDescriptor();
            uint8_t ifaceNum = desc->bInterfaceNumber;
            if (!fCommInterface && ifaceNum == fProbeCommIfNum) {
                IOLog("TL_WN722N::Found control interface: %d/%d/%d, opening.\n",
                    desc->bInterfaceClass, desc->bInterfaceSubClass,
                    desc->bInterfaceProtocol);
                if (!iface->open(this)) {
                    IOLog("TL_WN722N::Could not open RNDIS control interface.\n");
                    return false;
                }
                // Note, we retain AFTER opening the interface, because once
                // 'fCommInterface' is set, the 'closeUSBInterfaces' would
                // always try to close it before releasing:
                fCommInterface = retainT(iface);
            } else if (ifaceNum == fProbeCommIfNum + 1) {
                IOLog("TL_WN722N::Found data interface: %d/%d/%d, opening.\n",
                    desc->bInterfaceClass, desc->bInterfaceSubClass,
                    desc->bInterfaceProtocol);
                if (!iface->open(this)) {
                    IOLog("TL_WN722N::Could not open RNDIS data interface.\n");
                    return false;
                }
                // open before retain, see above:
                fDataInterface = retainT(iface);
                break;  // We should be done by now.
            }
        }
        OSSafeReleaseNULL(iterator);
    }
     
    if (!fCommInterface) {
        IOLog("TL_WN722N::could not find the required CONTROL interface, despite seeing their descriptors during 'probe' method call.\n");
        return false;
    }
    
    {  // Get the pipes for the CONTROL interface:
        const EndpointDescriptor *candidate = NULL;
        const InterfaceDescriptor *intDesc = fCommInterface->getInterfaceDescriptor();
        const ConfigurationDescriptor *confDesc = fCommInterface->getConfigurationDescriptor();
        
        IOLog("TL_WN722N::Expected 2 endpoints for CONTROL Interface, got: %d.\n", intDesc->bNumEndpoints);
        
        while((candidate = StandardUSB::getNextEndpointDescriptor(confDesc, intDesc, candidate)) != NULL)
        {
            uint8_t epDirection = getEndpointDirection(candidate);
            uint8_t epType = getEndpointType(candidate);
            IOLog("TL_WN722N::Endpoint found: direction = %d, type = %d.\n",epDirection, epType);
            if (kEndpointDirectionOut == epDirection && kEndpointTypeBulk == epType)
            {
                IOLog("TL_WN722N:: Found matching endpoint.\n");
                fOutPipe = fCommInterface->copyPipe(getEndpointAddress(candidate));
                if (fOutPipe == NULL)
                {
                    IOLog("TL_WN722N:: Copy pipe failed.\n");
                    return false;
                }
                else
                {
                    IOLog("TL_WN722N::Found fOutPipe.\n");
                    //Configure, WRITE FIRMWARE;
                    upgraded = performUpgrade();
                    if(upgraded == true)
                    {
                        IOLog("TL_WN722N:: Firmware upgraded successfully.\n");
                        return upgraded;
                    }
                    else{
                        IOLog("TL_WN722N:: Firmware NOT upgraded.\n");
                        return upgraded;
                    }
                    break;
                }
            }
        }
    }
    return true;
}

bool TL_WN722N::rndisInit() {
    int rc;
    union {
        struct rndis_msg_hdr *hdr;
        struct rndis_init *init;
        struct rndis_init_c *init_c;
    } u;
    
    u.hdr = (rndis_msg_hdr *)IOMallocAligned(RNDIS_CMD_BUF_SZ, sizeof(void *));
    if (!u.hdr) {
        IOLog("TL_WN722N::out of memory?");
        return false;
    }
    
    u.init->msg_type = RNDIS_MSG_INIT;
    u.init->msg_len = cpu_to_le32(sizeof *u.init);
    u.init->major_version = cpu_to_le32(1);
    u.init->minor_version = cpu_to_le32(0);
    // This is the maximum USB transfer the device is allowed to make to host:
    u.init->max_transfer_size = IN_BUF_SIZE;
    rc = rndisCommand(u.hdr, RNDIS_CMD_BUF_SZ);
    if (rc != kIOReturnSuccess) {
        IOLog("TL_WN722N::INIT not successful?");
        IOFreeAligned(u.hdr, RNDIS_CMD_BUF_SZ);
        return false;
    }

    if (fCommInterface) {  // Safety: don't accesss 'fCommInterface if NULL.
        IOLog("TL_WN722N::'%s': ver=%d.%d, max_packets_per_transfer=%d, "
            "max_transfer_size=%d, packet_alignment=2^%d",
            fCommInterface->getDevice()->getName(),
            le32_to_cpu(u.init_c->major_version),
            le32_to_cpu(u.init_c->minor_version),
            le32_to_cpu(u.init_c->max_packets_per_transfer),
            le32_to_cpu(u.init_c->max_transfer_size),
            le32_to_cpu(u.init_c->packet_alignment));
    }

    maxOutTransferSize = le32_to_cpu(u.init_c->max_transfer_size);
    // For now, let's limit the maxOutTransferSize by the Output Buffer size.
    // If we implement transmitting multiple PDUs in a single USB transfer,
    // we may want to size the output buffers based on
    // "u.init_c->max_transfer_size".
    maxOutTransferSize = min(maxOutTransferSize, OUT_BUF_SIZE);
    
    IOFreeAligned(u.hdr, RNDIS_CMD_BUF_SZ);
    
    return true;
}

bool TL_WN722N::performUpgrade()
{
    IOLog("TL_WN722N:: Performing firmware upgrade.\n");
    
    OSData *firmwareData = OSData::withBytes(ar9271_fw, ar9271_fw_len);
    bool success = true;
        
    IOReturn result;
    if (IOMemoryDescriptor *buffer = IOMemoryDescriptor::withAddress((void*)firmwareData->getBytesNoCopy(), firmwareData->getLength(), kIODirectionOut))
    {
        IOLog("TL_WN722N:: Calling buffer->prepare()\n");
        if ((result = buffer->prepare()) == kIOReturnSuccess)
        {
            IOLog("TL_WN722N:: Calling pipeCommand(I)\n");
            if ((result = pipeCommand(0x40, FIRMWARE_DOWNLOAD, AR9271_FIRMWARE >> 8, buffer)) != kIOReturnSuccess)
                IOLog("TL_WN722N:: Unable to write the firmware (0x%08x).\n", result);
            else
            {
                IOLog("TL_WN722N:: Calling pipeCommand(II)\n");
                if ((result = pipeCommand(0x40, FIRMWARE_DOWNLOAD_COMP, AR9271_FIRMWARE_TEXT >> 8, NULL, 0)) != kIOReturnSuccess)
                    IOLog("TL_WN722N:: Unable to write the firmware complete sequence (0x%08x).\n", result);
                else
                {
                    IOLog("TL_WN722N:: Success in writing the firmware sequence.\n");
                    success = true;
                }
            }
        }
        else
            IOLog("TL_WN722N:: Failed to prepare write memory buffer (0x%08x).\n", result);
        
        if ((result = buffer->complete()) != kIOReturnSuccess)
            IOLog("TL_WN722N:: Failed to complete write memory buffer (0x%08x).\n", result);
        
        OSSafeReleaseNULL(buffer);
    }
    else
        IOLog("TL_WN722N:: Unable to allocate write memory buffer.\n");
    
    OSSafeReleaseNULL(firmwareData);
    return success;
}

void TL_WN722N::closeUSBInterfaces() {
    fReadyToTransfer = false;  // Interfaces are about to be closed.
    // Close the interfaces - this would abort the transfers (if present):
    if (fDataInterface) {
        fDataInterface->close(this);
    }
    if (fCommInterface) {
        fCommInterface->close(this);
    }

    OSSafeReleaseNULL(fInPipe);
    OSSafeReleaseNULL(fOutPipe);
    OSSafeReleaseNULL(fDataInterface);
    OSSafeReleaseNULL(fCommInterface);  // First one to open, last one to die.
}

IOReturn TL_WN722N::pipeCommand(UInt8 requestType, UInt8 command, UInt16 address, IOMemoryDescriptor *buffer)
{
    DeviceRequest request;
    request.bmRequestType = requestType;
    request.bRequest = command;
    request.wValue = address;
    request.wIndex = 0;
    request.wLength = buffer->getLength();
    
    uint32_t bytesTransferred;
    return fCommInterface->deviceRequest(request, buffer, bytesTransferred, kUSBHostStandardRequestCompletionTimeout);
}

IOReturn TL_WN722N::pipeCommand(UInt8 requestType, UInt8 command, UInt16 address, void *buffer, UInt16 length)
{
    DeviceRequest request;
    request.bmRequestType = requestType;
    request.bRequest = command;
    request.wValue = address;
    request.wIndex = 0;
    request.wLength = length;
    
    uint32_t bytesTransferred;
    return fCommInterface->deviceRequest(request, buffer, bytesTransferred, kUSBHostStandardRequestCompletionTimeout);
}

bool TL_WN722N::createMediumTables()
{
    IOLog("TL_WN722N::Creating medium tables for AirPortAtheros9271 driver.\n");
    IONetworkMedium *medium;
    
    OSDictionary *mediumDict = OSDictionary::withCapacity(1);
    if (mediumDict == NULL)
    {
        IOLog("TL_WN722N::Cannot allocate medium dictionary.\n");
        return false;
    }
    
    medium = IONetworkMedium::medium(kIOMediumEthernetAuto, 480 * 1000000);
    IONetworkMedium::addMedium(mediumDict, medium);
    OSSafeReleaseNULL(medium);
    
    bool setResult = false;
    bool result = publishMediumDictionary(mediumDict);
    if (!result)
        IOLog("TL_WN722N::Cannot publish medium dictionary.\n");
    else
    {
        medium = IONetworkMedium::getMediumWithType(mediumDict, kIOMediumEthernetAuto);
        setResult = setCurrentMedium(medium);
    }
    
    OSSafeReleaseNULL(mediumDict);
    return result && setResult;
}

IOReturn TL_WN722N::getHardwareAddress(IOEthernetAddress *addr) //TODO: getHardwareAddress() dynamically
{
    /*UInt32      i;
    void *buf;
    unsigned char *bp;
    int rlen = -1;
    int rv;
    
    buf = IOMallocAligned(RNDIS_CMD_BUF_SZ, sizeof(void *));
    if (!buf) {
        return kIOReturnNoMemory;
    }

    // WARNING: Android devices may randomly-generate RNDIS MAC address.
    // The function may return different results for the same device.

    rv = rndisQuery(buf, OID_802_3_PERMANENT_ADDRESS, 48, (void **) &bp, &rlen);
    if (rv < 0) {
        IOLog("TL_WN722N::getHardwareAddress OID failed?");
        IOFreeAligned(buf, RNDIS_CMD_BUF_SZ);
        return kIOReturnIOError;
    }
    IOLog("TL_WN722N::MAC Address %02x:%02x:%02x:%02x:%02x:%02x -- rlen %d",
          bp[0], bp[1], bp[2], bp[3], bp[4], bp[5],
          rlen);
    
    for (i=0; i<6; i++) {
        addr->bytes[i] = bp[i];
    }
    
    IOFreeAligned(buf, RNDIS_CMD_BUF_SZ);
    return kIOReturnSuccess;*/
    addr->bytes[0] = 0xe8;
    addr->bytes[1] = 0x94;
    addr->bytes[2] = 0xf6;
    addr->bytes[3] = 0x10;
    addr->bytes[4] = 0xf1;
    addr->bytes[5] = 0x9d;
    return kIOReturnSuccess;
}

IOReturn TL_WN722N::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
    IOReturn result = kIOReturnSuccess;
    
    if (group == gIOEthernetWakeOnLANFilterGroup)
        *filters = 0;
    else if (group == gIONetworkFilterGroup)
    {
        *filters = kIOPacketFilterUnicast | kIOPacketFilterBroadcast | kIOPacketFilterPromiscuous | kIOPacketFilterMulticast | kIOPacketFilterMulticastAll;
    }
    else
    {
        result = super::getPacketFilters(group, filters);
    }
    return result;
}

/* Overrides IOEthernetController::createInterface */
IONetworkInterface *TL_WN722N::createInterface() {
    HoRNDISInterface *netif = new HoRNDISInterface;
    
    if (!netif) {
        return NULL;
    }

    int mtuLimit = maxOutTransferSize
        - (int)sizeof(rndis_data_hdr)
        - 14;  // Size of ethernet header (no QLANs). Checksum is not included.

    if (!netif->init(this, min(ETHERNET_MTU, mtuLimit))) {
        netif->release();
        return NULL;
    }
    
    return netif;
}

bool TL_WN722N::configureInterface(IONetworkInterface *netif) {
    IONetworkData *nd;
    
    if (super::configureInterface(netif) == false) {
        IOLog("TL_WN722N::super failed");
        return false;
    }
    
    nd = netif->getNetworkData(kIONetworkStatsKey);
    if (!nd || !(fpNetStats = (IONetworkStats *)nd->getBuffer())) {
        IOLog("TL_WN722N::network statistics buffer unavailable?\n");
        return false;
    }
    
    IOLog("TL_WN722N::fpNetStats: %p", fpNetStats);
    
    return true;
}


IOReturn TL_WN722N::enable(IONetworkInterface *netif) {
    IOReturn rtn = kIOReturnSuccess;

    /*IOLog("TL_WN722N::begin for thread_id=%lld", thread_tid(current_thread()));
    ReentryLocker locker(this, fEnableDisableInProgress);
    if (locker.isInterrupted()) {
        LOG(V_ERROR, "Waiting interrupted");
        return locker.getResult();
    }

    if (fNetifEnabled) {
        IOLog("TL_WN722N::Repeated call (thread_id=%lld), returning success",
            thread_tid(current_thread()));
        return kIOReturnSuccess;
    }

    if (fCallbackCount != 0) {
        IOLog("TL_WN722N::Invalid state: fCallbackCount(=%d) != 0", fCallbackCount);
        return kIOReturnError;
    }

    if (!allocateResources()) {
        return kIOReturnNoMemory;
    }

    // Tell the other end to start transmitting.
    if (!rndisSetPacketFilter(RNDIS_DEFAULT_FILTER)) {
        goto bailout;
    }

    // The pipe stall clearning is not needed for the first "enable" call after
    // pugging in the device, but it becomes necessary when "disable" is called
    // after that, followed by another "enable". This happens when user runs
    // "sudo ifconfig <netif> down", followed by "sudo ifconfig <netif> up"
    IOLog("TL_WN722N::Clearing potential Pipe stalls on Input and Output pipes");
    loopClearPipeStall(fInPipe);
    loopClearPipeStall(fOutPipe);

    // We can now perform reads and writes between Network stack and USB device:
    fReadyToTransfer = true;
    
    // Kick off the read requests:
    for (int i = 0; i < N_IN_BUFS; i++) {
        pipebuf_t &inbuf = inbufs[i];
        inbuf.comp.owner = this;
        inbuf.comp.action = dataReadComplete;
        inbuf.comp.parameter = &inbuf;

        rtn = robustIO(fInPipe, &inbuf, (uint32_t)inbuf.mdp->getLength());
        if (rtn != kIOReturnSuccess) {
            IOLog("TL_WN722N::Failed to start the first read: %08x\n", rtn);
            goto bailout;
        }
        fCallbackCount++;
    }

    // Tell the world that the link is up...
    if (!setLinkStatus(kIONetworkLinkActive | kIONetworkLinkValid,
            getCurrentMedium())) {
        IOLog("TL_WN722N::Cannot set link status");
        rtn = kIOReturnError;
        goto bailout;
    }

    // ... and then listen for packets!
    getOutputQueue()->setCapacity(TRANSMIT_QUEUE_SIZE);
    getOutputQueue()->start();
    IOLog("TL_WN722N::txqueue started");

    // Now we can say we're alive.
    fNetifEnabled = true;
    IOLog("TL_WN722N::completed (thread_id=%lld): RNDIS network interface '%s' "
        "should be live now", thread_tid(current_thread()), netif->getName());
    
    return kIOReturnSuccess;
    
bailout:
    disableImpl();*/
    return rtn;
}

IOReturn TL_WN722N::disable(IONetworkInterface *netif) {
    /*LOG(V_DEBUG, "begin for thread_id=%lld", thread_tid(current_thread()));
    // This function can be called as a consequence of:
    //  1. USB Disconnect
    //  2. Some action, while the device is up and running
    //     (e.g. "ifconfig en6 down").
    // In the second case, we'll need to do more cleanup:
    // ask the RNDIS device to stop transmitting, and abort the callbacks.
    //

    ReentryLocker locker(this, fEnableDisableInProgress);
    if (locker.isInterrupted()) {
        LOG(V_ERROR, "Waiting interrupted");
        return locker.getResult();
    }

    if (!fNetifEnabled) {
        LOG(V_DEBUG, "Repeated call (thread_id=%lld)", thread_tid(current_thread()));
        return kIOReturnSuccess;
    }

    disableImpl();

    LOG(V_DEBUG, "completed (thread_id=%lld)", thread_tid(current_thread()));*/
    return kIOReturnSuccess;
}

IOReturn TL_WN722N::selectMedium(const IONetworkMedium *medium)
{
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

IOReturn TL_WN722N::setMulticastMode(bool active)
{
    return kIOReturnSuccess;
}

IOReturn TL_WN722N::setMulticastList(IOEthernetAddress *addrs, UInt32 count)
{
    return kIOReturnSuccess;
}

IOReturn TL_WN722N::setPromiscuousMode(bool active)
{
    return kIOReturnSuccess;
}



bool HoRNDISInterface::init(IONetworkController *controller, int mtu) {
    maxmtu = mtu;
    if (IOEthernetInterface::init(controller) == false) {
        return false;
    }
    IOLog("TL_WN722N::(network interface) starting up with MTU %d.\n", mtu);
    setMaxTransferUnit(mtu);
    return true;
}

bool HoRNDISInterface::setMaxTransferUnit(UInt32 mtu) {
    if (mtu > maxmtu) {
        IOLog("TL_WN722N::Excuse me, but I said you could have an MTU of %u, and you just tried to set an MTU of %d.  Good try, buddy..\n", maxmtu, mtu);
        return false;
    }
    IOEthernetInterface::setMaxTransferUnit(mtu);
    return true;
}

int TL_WN722N::rndisQuery(void *buf, uint32_t oid, uint32_t in_len, void **reply, int *reply_len) {
    int rc;
    
    union {
        void *buf;
        struct rndis_msg_hdr *hdr;
        struct rndis_query *get;
        struct rndis_query_c *get_c;
    } u;
    uint32_t off, len;
    
    u.buf = buf;
    
    memset(u.get, 0, sizeof(*u.get) + in_len);
    u.get->msg_type = RNDIS_MSG_QUERY;
    u.get->msg_len = cpu_to_le32(sizeof(*u.get) + in_len);
    u.get->oid = oid;
    u.get->len = cpu_to_le32(in_len);
    u.get->offset = cpu_to_le32(20);
    
    rc = rndisCommand(u.hdr, RNDIS_CMD_BUF_SZ);
    if (rc != kIOReturnSuccess) {
        IOLog("TL_WN722N::RNDIS_MSG_QUERY failure? %08x.\n", rc);
        return rc;
    }
    
    off = le32_to_cpu(u.get_c->offset);
    len = le32_to_cpu(u.get_c->len);
    IOLog("TL_WN722N::RNDIS query completed.\n");
    
    if ((8 + off + len) > RNDIS_CMD_BUF_SZ) {
        goto fmterr;
    }
    if (*reply_len != -1 && len != *reply_len) {
        goto fmterr;
    }
    
    *reply = ((unsigned char *) &u.get_c->request_id) + off;
    *reply_len = len;
    
    return 0;

fmterr:
    IOLog("TL_WN722N::protocol error?\n");
    return -1;
}


/***** RNDIS command logic *****/

IOReturn TL_WN722N::rndisCommand(struct rndis_msg_hdr *buf, int buflen) {
    int rc = kIOReturnSuccess;
    if (!fCommInterface) {  // Safety: make sure 'fCommInterface' is valid.
        IOLog("TL_WN722N::fCommInterface is NULL, bailing out.\n");
        return kIOReturnError;
    }
    const uint8_t ifNum = fCommInterface->getInterfaceDescriptor()->bInterfaceNumber;

    if (buf->msg_type != RNDIS_MSG_HALT && buf->msg_type != RNDIS_MSG_RESET) {
        // No need to lock here: multi-threading does not even come close
        // (IOWorkLoop + IOGate are at our service):
        buf->request_id = cpu_to_le32(rndisXid++);
        if (!buf->request_id) {
            buf->request_id = cpu_to_le32(rndisXid++);
        }
        
        IOLog("TL_WN722N::Generated xid: %d.\n", le32_to_cpu(buf->request_id));
    }
    const uint32_t old_msg_type = buf->msg_type;
    const uint32_t old_request_id = buf->request_id;
    
    {
        DeviceRequest rq;
        rq.bmRequestType = kDeviceRequestDirectionOut |
            kDeviceRequestTypeClass | kDeviceRequestRecipientInterface;
        rq.bRequest = USB_CDC_SEND_ENCAPSULATED_COMMAND;
        rq.wValue = 0;
        rq.wIndex = ifNum;
        rq.wLength = le32_to_cpu(buf->msg_len);
    
        uint32_t bytes_transferred;
        if ((rc = fCommInterface->deviceRequest(rq, buf, bytes_transferred)) != kIOReturnSuccess) {
            IOLog("TL_WN722N::Device request send error.\n");
            return rc;
        }
        if (bytes_transferred != rq.wLength) {
            IOLog("TL_WN722N::Incomplete device transfer.\n");
            return kIOReturnError;
        }
    }

    // The RNDIS control messages are done via 'deviceRequest' - issue control
    // transfers on the device's default endpoint. Per [MSDN-RNDISUSB], if
    // a device is not ready (for some reason) to reply with the actual data,
    // it shall send a one-byte reply indicating an error, rather than stall
    // the control pipe. The retry loop below is a hackish way of waiting
    // for the reply.
    //
    // Per [MSDN-RNDISUSB], once the driver sends a OUT device transfer, it
    // should wait for a notification on the interrupt endpoint from
    // fCommInterface, and only then perform a device request to retrieve
    // the result. Whether Android does that correctly is something I need to
    // investigate.
    //
    // Also, RNDIS specifies that the device may be sending
    // REMOTE_NDIS_INDICATE_STATUS_MSG on its own. How much this applies to
    // Android or embedded Linux devices needs to be investigated.
    //
    // Reference:
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/network/control-channel-characteristics

    // Now we wait around a while for the device to get back to us.
    int count;
    for (count = 0; count < 10; count++) {
        DeviceRequest rq;
        rq.bmRequestType = kDeviceRequestDirectionIn |
            kDeviceRequestTypeClass | kDeviceRequestRecipientInterface;
        rq.bRequest = USB_CDC_GET_ENCAPSULATED_RESPONSE;
        rq.wValue = 0;
        rq.wIndex = ifNum;
        rq.wLength = RNDIS_CMD_BUF_SZ;

        // Make sure 'fCommInterface' was not taken away from us while
        // we were doing synchronous IO:
        if (!fCommInterface) {
            IOLog("TL_WN722N::fCommInterface was closed, bailing out.\n");
            return kIOReturnError;
        }
        uint32_t bytes_transferred;
        if ((rc = fCommInterface->deviceRequest(rq, buf, bytes_transferred)) != kIOReturnSuccess) {
            return rc;
        }

        if (bytes_transferred < 12) {
            IOLog("TL_WN722N::short read on control request?\n");
            IOSleep(20);
            continue;
        }
        
        if (buf->msg_type == (old_msg_type | RNDIS_MSG_COMPLETION)) {
            if (buf->request_id == old_request_id) {
                if (buf->msg_type == RNDIS_MSG_RESET_C) {
                    // This is probably incorrect: the RESET_C does not have
                    // 'request_id', but we don't issue resets => don't care.
                    break;
                }
                if (buf->status != RNDIS_STATUS_SUCCESS) {
                    IOLog("TL_WN722N::RNDIS command returned status %08x.\n",
                        le32_to_cpu(buf->status));
                    rc = kIOReturnError;
                    break;
                }
                if (le32_to_cpu(buf->msg_len) != bytes_transferred) {
                    IOLog("TL_WN722N::Message Length mismatch: expected: %d, actual: %d.\n",
                        le32_to_cpu(buf->msg_len), bytes_transferred);
                    rc = kIOReturnError;
                    break;
                }
                IOLog("TL_WN722N::RNDIS command completed.\n");
                break;
            } else {
                IOLog("TL_WN722N::RNDIS return had incorrect xid?\n");
            }
        } else {
            if (buf->msg_type == RNDIS_MSG_INDICATE) {
                IOLog("TL_WN722N::unsupported: RNDIS_MSG_INDICATE.\n");
            } else if (buf->msg_type == RNDIS_MSG_INDICATE) {
                IOLog("TL_WN722N::unsupported: RNDIS_MSG_KEEPALIVE.\n");
            } else {
                IOLog("TL_WN722N::unexpected msg type %08x, msg_len %08x\n",
                    le32_to_cpu(buf->msg_type), le32_to_cpu(buf->msg_len));
            }
        }
        
        IOSleep(20);
    }
    if (count == 10) {
        IOLog("TL_WN722N::command timed out?\n");
        return kIOReturnTimeout;
    }

    return rc;
}

bool TL_WN722N::createMediumTables(const IONetworkMedium **primary) {
    IONetworkMedium    *medium;
    
    OSDictionary *mediumDict = OSDictionary::withCapacity(1);
    if (mediumDict == NULL) {
        IOLog("TL_WN722N::Cannot allocate OSDictionary.\n");
        return false;
    }
    
    medium = IONetworkMedium::medium(kIOMediumEthernetAuto, 480 * 1000000);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();  // 'mediumDict' holds a ref now.
    if (primary) {
        *primary = medium;
    }
    
    bool result = publishMediumDictionary(mediumDict);
    if (!result) {
        IOLog("TL_WN722N::Cannot publish medium dictionary!\n");
    }

    // Per comment for 'publishMediumDictionary' in NetworkController.h, the
    // medium dictionary is copied and may be safely relseased after the call.
    mediumDict->release();
    
    return result;
}

bool TL_WN722N::initForPM(IOService *provider)
{
    IOLog("TL_WN722N::Configuring Device - Initializing Power Manager\n");
    
    fPowerState = kCDCPowerOnState;                // init our power state to be 'on'
    PMinit();                            // init power manager instance variables
    provider->joinPMtree(this);                    // add us to the power management tree
    if (pm_vars != NULL)
    {
        
        // register ourselves with ourself as policy-maker
        
        registerPowerDriver(this, gOurPowerStates, kNumCDCStates);
        return true;
    } else {
        IOLog("TL_WN722N::Configuring Device - Initializing Power Manager Failed\n");
    }
    
    return false;
    
}/* end initForPM */
