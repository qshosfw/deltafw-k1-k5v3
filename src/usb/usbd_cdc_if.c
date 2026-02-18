#include "usbd_core.h"
#include "usbd_cdc.h"
#include "drivers/bsp/py25q16.h"
#include "helper/identifier.h"
#include "apps/settings/settings.h"
#include "ui/helper.h"

/*!< endpoint address */
#define CDC_IN_EP  0x81
#define CDC_OUT_EP 0x02
#define CDC_INT_EP 0x83

#define USBD_VID           0x36b7
#define USBD_PID           0xFFFF
#define USBD_MAX_POWER     100
#define USBD_LANGID_STRING 1033

/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

uint8_t dma_in_ep_idx  = (CDC_IN_EP & 0x7f);
uint8_t dma_out_ep_idx = CDC_OUT_EP;

/*!< global descriptor */
static uint8_t cdc_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01),
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, 0x02),
    ///////////////////////////////////////
    /// string0 descriptor
    ///////////////////////////////////////
    USB_LANGID_INIT(USBD_LANGID_STRING),
    ///////////////////////////////////////
    /// string1 descriptor
    ///////////////////////////////////////
    0x0A,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'P', 0x00,                  /* wcChar0 */
    'U', 0x00,                  /* wcChar1 */
    'Y', 0x00,                  /* wcChar2 */
    'A', 0x00,                  /* wcChar3 */
    ///////////////////////////////////////
    /// string2 descriptor
    ///////////////////////////////////////
    0x2A,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    'Q', 0x00,                  /* wcChar0 */
    'u', 0x00,                  /* wcChar1 */
    'a', 0x00,                  /* wcChar2 */
    'n', 0x00,                  /* wcChar3 */
    's', 0x00,                  /* wcChar4 */
    'h', 0x00,                  /* wcChar5 */
    'e', 0x00,                  /* wcChar6 */
    'n', 0x00,                  /* wcChar7 */
    'g', 0x00,                  /* wcChar8 */
    ' ', 0x00,                  /* wcChar9 */
    'U', 0x00,                  /* wcChar10 */
    'V', 0x00,                  /* wcChar11 */
    '-', 0x00,                  /* wcChar12 */
    'K', 0x00,                  /* wcChar13 */
    '5', 0x00,                  /* wcChar14 */
    ' ', 0x00,                  /* wcChar15 */
    'X', 0x00,                  /* wcChar16 */
    'X', 0x00,                  /* wcChar17 */
    'X', 0x00,                  /* wcChar18 */
    'X', 0x00,                  /* wcChar19 */
    ///////////////////////////////////////
    /// string3 descriptor
    ///////////////////////////////////////
    ///////////////////////////////////////
    /// string3 descriptor
    ///////////////////////////////////////
    0x1E,                       /* bLength */
    USB_DESCRIPTOR_TYPE_STRING, /* bDescriptorType */
    '0', 0x00,                  /* wcChar0 */
    '0', 0x00,                  /* wcChar1 */
    '0', 0x00,                  /* wcChar2 */
    '0', 0x00,                  /* wcChar3 */
    '0', 0x00,                  /* wcChar4 */
    '0', 0x00,                  /* wcChar5 */
    '0', 0x00,                  /* wcChar6 */
    '0', 0x00,                  /* wcChar7 */
    '0', 0x00,                  /* wcChar8 */
    '0', 0x00,                  /* wcChar9 */
    '0', 0x00,                  /* wcChar10 */
    '0', 0x00,                  /* wcChar11 */
    '0', 0x00,                  /* wcChar12 */
    '0', 0x00,                  /* wcChar13 */
#ifdef CONFIG_USB_HS
    ///////////////////////////////////////
    /// device qualifier descriptor
    ///////////////////////////////////////
    0x0a,
    USB_DESCRIPTOR_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x01,
    0x00,
#endif
    0x00
};

USB_MEM_ALIGNX uint8_t read_buffer[128];
// USB_MEM_ALIGNX uint8_t write_buffer[4];

static cdc_acm_rx_buf_t client_rx_buf = {0};

volatile bool ep_tx_busy_flag = false;

#ifdef CONFIG_USB_HS
#define CDC_MAX_MPS 512
#else
#define CDC_MAX_MPS 64
#endif

void usbd_configure_done_callback(void)
{
    /* setup first out ep read transfer */
    usbd_ep_start_read(CDC_OUT_EP, read_buffer, sizeof(read_buffer));
}

void usbd_cdc_acm_bulk_out(uint8_t ep, uint32_t nbytes)
{
    cdc_acm_rx_buf_t *rx_buf = &client_rx_buf;
    if (nbytes && rx_buf->buf)
    {
        const uint8_t *buf = read_buffer;
        uint32_t pointer = *rx_buf->write_pointer;
        while (nbytes)
        {
            const uint32_t rem = rx_buf->size - pointer;
            if (0 == rem)
            {
                pointer = 0;
                continue;
            }

            uint32_t size = rem < nbytes ? rem : nbytes;
            memcpy(rx_buf->buf + pointer, buf, size);
            buf += size;
            nbytes -= size;
            pointer += size;
        }

        *rx_buf->write_pointer = pointer;
    }

    /* setup next out ep read transfer */
    usbd_ep_start_read(CDC_OUT_EP, read_buffer, sizeof(read_buffer));
}

void usbd_cdc_acm_bulk_in(uint8_t ep, uint32_t nbytes)
{
    if ((nbytes % CDC_MAX_MPS) == 0 && nbytes) {
        /* send zlp */
        usbd_ep_start_write(CDC_IN_EP, NULL, 0);
    } else {
        ep_tx_busy_flag = false;
    }
}

/*!< endpoint call back */
struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_bulk_out
};

struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_bulk_in
};

struct usbd_interface intf0;
struct usbd_interface intf1;

void cdc_acm_init(cdc_acm_rx_buf_t rx_buf)
{
    // client_rx_buf = rx_buf;
    memcpy(&client_rx_buf, &rx_buf, sizeof(cdc_acm_rx_buf_t));
    *client_rx_buf.write_pointer = 0;

    memcpy(&client_rx_buf, &rx_buf, sizeof(cdc_acm_rx_buf_t));
    *client_rx_buf.write_pointer = 0;

    // Detect Model (K1 vs K5)
    // gEeprom.SET_NAV == 0 => "K1 (L/R)"
    // gEeprom.SET_NAV == 1 => "K5 (U/D)"
    // Old logic: "UV-K5". Patch '5'.
    // New logic: "UV-K5" or "UV-K1".
    
#ifdef ENABLE_IDENTIFIER
    // Get MAC/Serial info
    char crockford[20];
    GetCrockfordSerial(crockford);
    // Format: AAAA/BBBB/CCCC/D*
    // We want last 4 chars of the serial part? "last 4 caps chars of mac address"
    // User said: "last 4 caps chars of mac address".
    // GetMacAddress returns 6 bytes.
    uint8_t mac[6];
    GetMacAddress(mac);
    // Convert last 2 bytes to Hex string? Or last 4 hex digits?
    // "last 4 caps chars of mac address" -> likely last 2 bytes printed as hex.
    char macLast4[5];
    // sprintf(macLast4, "%02X%02X", mac[4], mac[5]);
    NUMBER_ToHex(macLast4, mac[4], 2);
    NUMBER_ToHex(macLast4 + 2, mac[5], 2);
    macLast4[4] = '\0';
    
    // Patch PID (Bytes 10, 11 of Device Descriptor)
    cdc_descriptor[10] = mac[5];
    cdc_descriptor[11] = mac[4];
#endif
    
    // Find String 2 Start ('Q' 'u' 'a' 'n'...)
    // We know the approximate location or we can scan.
    // Scanning is safer against descriptor length changes.
    int str2Idx = -1;
    for (int i = 0; i < sizeof(cdc_descriptor) - 40; i++) {
        if (cdc_descriptor[i] == 'Q' && cdc_descriptor[i+2] == 'u' && cdc_descriptor[i+4] == 'a') {
            str2Idx = i;
            break;
        }
    }

    if (str2Idx >= 0) {
        // Update Model Char
        // 'Q' is at str2Idx
        // "Quansheng " is 10 chars (0-9)
        // "UV-K5" -> U(10), V(11), -(12), K(13), 5(14)
        // Index 14 * 2 = 28
        // If SET_NAV == 0 (K1), set '1'. 
        // If SET_NAV == 1 (K5), set '5'. 
        // Default might be K5 but if SET_NAV=0 it becomes K1.
        cdc_descriptor[str2Idx + 28] = (gEeprom.SET_NAV == 0) ? '1' : '5';
        
#ifdef ENABLE_IDENTIFIER
        // Update XXXX (Index 16, 17, 18, 19) -> 32, 34, 36, 38
        cdc_descriptor[str2Idx + 32] = macLast4[0];
        cdc_descriptor[str2Idx + 34] = macLast4[1];
        cdc_descriptor[str2Idx + 36] = macLast4[2];
        cdc_descriptor[str2Idx + 38] = macLast4[3];
        
        // Update String 3 (Serial)
        int str3DataIdx = str2Idx + 42;
        
        // Crockford Serial is in 'crockford' (14 chars)
        for (int i = 0; i < 14; i++) {
             cdc_descriptor[str3DataIdx + (i * 2)] = crockford[i];
             cdc_descriptor[str3DataIdx + (i * 2) + 1] = 0x00;
        }
#endif
    }


    usbd_desc_register(cdc_descriptor);
    usbd_add_interface(usbd_cdc_acm_init_intf(&intf0));
    usbd_add_interface(usbd_cdc_acm_init_intf(&intf1));
    usbd_add_endpoint(&cdc_out_ep);
    usbd_add_endpoint(&cdc_in_ep);
    usbd_initialize();
}

volatile uint8_t dtr_enable = 0;

void usbd_cdc_acm_set_dtr(uint8_t intf, bool dtr)
{
    if (dtr) {
        dtr_enable = 1;
    } else {
        dtr_enable = 0;
    }
}

void cdc_acm_data_send_with_dtr(const uint8_t *buf, uint32_t size)
{
    if (dtr_enable && 0 != size)
    {
        while (ep_tx_busy_flag)
            ;
        ep_tx_busy_flag = true;
        usbd_ep_start_write(CDC_IN_EP, buf, size);
        while (ep_tx_busy_flag)
            ;
    }
}

void cdc_acm_data_send_with_dtr_async(const uint8_t *buf, uint32_t size)
{
    if (dtr_enable && 0 != size)
    {
        while (ep_tx_busy_flag)
            ;
        ep_tx_busy_flag = true;
        usbd_ep_start_write(CDC_IN_EP, buf, size);
    }
}
