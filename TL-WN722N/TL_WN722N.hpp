/* TL_WN722N class */

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#pragma clang diagnostic ignored "-Wdocumentation"
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IOOutputQueue.h>
#include <IOKit/IOService.h>
#pragma clang diagnostic pop

#define AR9271_FIRMWARE 0x501000
#define AR9271_FIRMWARE_TEXT 0x903000

#define FIRMWARE_DOWNLOAD 0x30
#define FIRMWARE_DOWNLOAD_COMP 0x31

#define cpu_to_le32(x) OSSwapHostToLittleInt32(x)
#define le32_to_cpu(x) OSSwapLittleToHostInt32(x)

// Maximum payload size in a standard (non-jumbo) Ethernet frame.
#define ETHERNET_MTU            1500
/***** RNDIS definitions -- from linux/include/linux/usb/rndis_host.h ****/

// Per [MSDN-RNDISUSB], "Control Channel Characteristics", it's the minimum
// buffer size the host should support (and it's way bigger than we need).
#define RNDIS_CMD_BUF_SZ        0x400

enum
{
    CS_INTERFACE        = 0x24,
    Header_FunctionalDescriptor    = 0x00,
    Union_FunctionalDescriptor    = 0x06,
    WCM_FunctionalDescriptor    = 0x11,
    kCDCPowerOffState    = 0,
    kCDCPowerOnState    = 1,
    kNumCDCStates    = 2
};

static IOPMPowerState gOurPowerStates[kNumCDCStates] =
{
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,IOPMDeviceUsable,IOPMPowerOn,IOPMPowerOn,0,0,0,0,0,0,0,0}
};

struct rndis_msg_hdr {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t status;
} __attribute__((packed));

struct rndis_data_hdr {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t data_offset;
    uint32_t data_len;
    
    uint32_t oob_data_offset;
    uint32_t oob_data_len;
    uint32_t num_oob;
    uint32_t packet_data_offset;
    
    uint32_t packet_data_len;
    uint32_t vc_handle;
    uint32_t reserved;
} __attribute__((packed));

struct rndis_query {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t oid;
    uint32_t len;
    uint32_t offset;
    uint32_t handle;
} __attribute__((packed));

struct rndis_query_c {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t status;
    uint32_t len;
    uint32_t offset;
} __attribute__((packed));

struct rndis_init {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t major_version;
    uint32_t minor_version;
    uint32_t max_transfer_size;
} __attribute__((packed));

struct rndis_init_c {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t status;
    uint32_t major_version;
    uint32_t minor_version;
    uint32_t device_flags;
    uint32_t medium;
    uint32_t max_packets_per_transfer;
    uint32_t max_transfer_size;
    uint32_t packet_alignment;
    uint32_t af_list_offset;
    uint32_t af_list_size;
} __attribute__((packed));

struct rndis_set {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t oid;
    uint32_t len;
    uint32_t offset;
    uint32_t handle;
} __attribute__((packed));

struct rndis_set_c {
    uint32_t msg_type;
    uint32_t msg_len;
    uint32_t request_id;
    uint32_t status;
} __attribute__((packed));

#define RNDIS_MSG_COMPLETION                    cpu_to_le32(0x80000000)
#define RNDIS_MSG_PACKET                        cpu_to_le32(0x00000001) /* 1-N packets */
#define RNDIS_MSG_INIT                          cpu_to_le32(0x00000002)
#define RNDIS_MSG_INIT_C                        (RNDIS_MSG_INIT|RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_HALT                          cpu_to_le32(0x00000003)
#define RNDIS_MSG_QUERY                         cpu_to_le32(0x00000004)
#define RNDIS_MSG_QUERY_C                       (RNDIS_MSG_QUERY|RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_SET                           cpu_to_le32(0x00000005)
#define RNDIS_MSG_SET_C                         (RNDIS_MSG_SET|RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_RESET                         cpu_to_le32(0x00000006)
#define RNDIS_MSG_RESET_C                       (RNDIS_MSG_RESET|RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_INDICATE                      cpu_to_le32(0x00000007)
#define RNDIS_MSG_KEEPALIVE                     cpu_to_le32(0x00000008)
#define RNDIS_MSG_KEEPALIVE_C                   (RNDIS_MSG_KEEPALIVE|RNDIS_MSG_COMPLETION)

#define RNDIS_STATUS_SUCCESS                    cpu_to_le32(0x00000000)
#define RNDIS_STATUS_FAILURE                    cpu_to_le32(0xc0000001)
#define RNDIS_STATUS_INVALID_DATA               cpu_to_le32(0xc0010015)
#define RNDIS_STATUS_NOT_SUPPORTED              cpu_to_le32(0xc00000bb)
#define RNDIS_STATUS_MEDIA_CONNECT              cpu_to_le32(0x4001000b)
#define RNDIS_STATUS_MEDIA_DISCONNECT           cpu_to_le32(0x4001000c)
#define RNDIS_STATUS_MEDIA_SPECIFIC_INDICATION  cpu_to_le32(0x40010012)

#define RNDIS_PHYSICAL_MEDIUM_UNSPECIFIED       cpu_to_le32(0x00000000)
#define RNDIS_PHYSICAL_MEDIUM_WIRELESS_LAN      cpu_to_le32(0x00000001)
#define RNDIS_PHYSICAL_MEDIUM_CABLE_MODEM       cpu_to_le32(0x00000002)
#define RNDIS_PHYSICAL_MEDIUM_PHONE_LINE        cpu_to_le32(0x00000003)
#define RNDIS_PHYSICAL_MEDIUM_POWER_LINE        cpu_to_le32(0x00000004)
#define RNDIS_PHYSICAL_MEDIUM_DSL               cpu_to_le32(0x00000005)
#define RNDIS_PHYSICAL_MEDIUM_FIBRE_CHANNEL     cpu_to_le32(0x00000006)
#define RNDIS_PHYSICAL_MEDIUM_1394              cpu_to_le32(0x00000007)
#define RNDIS_PHYSICAL_MEDIUM_WIRELESS_WAN      cpu_to_le32(0x00000008)
#define RNDIS_PHYSICAL_MEDIUM_MAX               cpu_to_le32(0x00000009)

#define OID_802_3_PERMANENT_ADDRESS             cpu_to_le32(0x01010101)
#define OID_GEN_MAXIMUM_FRAME_SIZE              cpu_to_le32(0x00010106)
#define OID_GEN_CURRENT_PACKET_FILTER           cpu_to_le32(0x0001010e)
#define OID_GEN_PHYSICAL_MEDIUM                 cpu_to_le32(0x00010202)

/* packet filter bits used by OID_GEN_CURRENT_PACKET_FILTER */
#define RNDIS_PACKET_TYPE_DIRECTED              cpu_to_le32(0x00000001)
#define RNDIS_PACKET_TYPE_MULTICAST             cpu_to_le32(0x00000002)
#define RNDIS_PACKET_TYPE_ALL_MULTICAST         cpu_to_le32(0x00000004)
#define RNDIS_PACKET_TYPE_BROADCAST             cpu_to_le32(0x00000008)
#define RNDIS_PACKET_TYPE_SOURCE_ROUTING        cpu_to_le32(0x00000010)
#define RNDIS_PACKET_TYPE_PROMISCUOUS           cpu_to_le32(0x00000020)
#define RNDIS_PACKET_TYPE_SMT                   cpu_to_le32(0x00000040)
#define RNDIS_PACKET_TYPE_ALL_LOCAL             cpu_to_le32(0x00000080)
#define RNDIS_PACKET_TYPE_GROUP                 cpu_to_le32(0x00001000)
#define RNDIS_PACKET_TYPE_ALL_FUNCTIONAL        cpu_to_le32(0x00002000)
#define RNDIS_PACKET_TYPE_FUNCTIONAL            cpu_to_le32(0x00004000)
#define RNDIS_PACKET_TYPE_MAC_FRAME             cpu_to_le32(0x00008000)

/* default filter used with RNDIS devices */
#define RNDIS_DEFAULT_FILTER ( \
        RNDIS_PACKET_TYPE_DIRECTED | \
        RNDIS_PACKET_TYPE_BROADCAST | \
        RNDIS_PACKET_TYPE_ALL_MULTICAST | \
        RNDIS_PACKET_TYPE_PROMISCUOUS)

#define USB_CDC_SEND_ENCAPSULATED_COMMAND       0x00
#define USB_CDC_GET_ENCAPSULATED_RESPONSE       0x01

#define OUT_BUF_SIZE            4096
// Per [MS-RNDIS], description of REMOTE_NDIS_INITIALIZE_MSG:
//    "MaxTransferSize (4 bytes): ... It SHOULD be set to 0x00004000"
// I.e. specs recommends we should be able to input 16K in a single transfer.
// Also, some Android versions (e.g. 8.1.0 on Pixel 2) seem to ignore
// "max_transfer_size" in "REMOTE_NDIS_INITIALIZE_MSG" and use packets up to
// 16K regardless.
#define IN_BUF_SIZE             16384

class TL_WN722N : public IOEthernetController
{
    OSDeclareDefaultStructors(TL_WN722N);
    
protected:
    typedef IOEthernetController super;
    
    bool performUpgrade();
    bool createMediumTables();
    bool createMediumTables(const IONetworkMedium **primary);
    IOReturn pipeCommand(UInt8 requestType, UInt8 command, UInt16 address, IOMemoryDescriptor *buffer);
    IOReturn pipeCommand(UInt8 requestType, UInt8 command, UInt16 address, void *buffer, UInt16 length);
private:
    UInt8            fPowerState;                // Ordinal for power
    // These pass information from 'probe' to 'openUSBInterfaces':
    uint8_t fProbeConfigVal;
    uint8_t fProbeCommIfNum;  // The data interface number is +1.
    
    // USB Communication:
    IOUSBHostInterface *fCommInterface;
    IOUSBHostInterface *fDataInterface;
    
    IOUSBHostPipe *fInPipe;
    IOUSBHostPipe *fOutPipe;
    
    IONetworkStats *fpNetStats;
    int32_t maxOutTransferSize;  // Set by 'rdisInit' from device reply.
    
    IOEthernetInterface *fNetworkInterface;
    
    uint32_t rndisXid;  // RNDIS request_id count.
    
    bool fReadyToTransfer;  // Ready to transmit: Android <-> MAC.
    // Set to true when 'enable' succeeds, and
    // set to false when 'disable' succeeds:

    bool rndisInit();
    IOService *probeDevice(IOUSBHostDevice *device, SInt32 *score);
    bool openUSBInterfaces(IOService *provider);
    void closeUSBInterfaces();
    bool createNetworkInterface(void);
    int rndisQuery(void *buf, uint32_t oid, uint32_t in_len, void **reply, int *reply_len);
    IOReturn rndisCommand(struct rndis_msg_hdr *buf, int buflen);

public:    
    virtual bool init(OSDictionary *dictionary = 0) override;
    virtual void free(void) override;
    
    virtual IOService *probe(IOService *provider, SInt32 *score) override;
    
    virtual bool attach(IOService *provider) override;
    virtual void detach(IOService *provider) override;
    
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;
    
    // IOEtherenetController overrides
    virtual IOReturn getHardwareAddress(IOEthernetAddress *addr) override;
    virtual IOReturn getMaxPacketSize(UInt32 *maxSize) const override;
    virtual IOReturn getPacketFilters(const OSSymbol *group, UInt32 *filters) const override;
    
    virtual IONetworkInterface *createInterface() override;
    virtual bool configureInterface(IONetworkInterface *iface) override;
    
    virtual IOReturn enable(IONetworkInterface *iface) override;
    virtual IOReturn disable(IONetworkInterface *iface) override;
    virtual IOReturn selectMedium(const IONetworkMedium *medium) override;
    virtual IOReturn setMulticastMode(bool active) override;
    virtual IOReturn setMulticastList(IOEthernetAddress *addrs, UInt32 count) override;
    virtual IOReturn setPromiscuousMode(bool active) override;
    bool initForPM(IOService *provider);
};

class HoRNDISInterface : public IOEthernetInterface {
    OSDeclareDefaultStructors(HoRNDISInterface);
    int maxmtu;
public:
    virtual bool init(IONetworkController *controller, int mtu);
    virtual bool setMaxTransferUnit(UInt32 mtu) override;
};
