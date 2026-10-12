/**
 * @file hid_iokit.c
 * @brief macOS IOKit implementation of OpenLogi HID transport.
 */

#ifdef __APPLE__

#include "hid_transport.h"
#include "hidpp.h"

#include <IOKit/hid/IOHIDManager.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#define MAX_CACHED_DEVICES 32

/* Collection match table: (UsagePage, Usage, is_long_only) */
static const struct {
    uint16_t page;
    uint16_t usage;
    bool long_only;
} G_COLLECTIONS[] = {
    { 0xFF00, 0x0002, false },  /* USB, Bolt, Unifying, BT-classic */
    { 0xFF43, 0x0202, true  },  /* Bluetooth Low Energy direct */
    { 0xFF43, 0x0602, false }   /* Wired G-series gaming keyboards */
};

#define NUM_COLLECTIONS (sizeof(G_COLLECTIONS) / sizeof(G_COLLECTIONS[0]))

#include "../../hid/hid_device_internal.h"


static IOHIDManagerRef g_hid_manager = NULL;
static hid_device_t *g_devices[MAX_CACHED_DEVICES] = { 0 };
static size_t g_device_count = 0;
static hid_hotplug_callback_t g_hotplug_cb = NULL;
static void *g_hotplug_user_data = NULL;

/* Helper to convert CFStringRef to C string */
static void cfstring_to_cstr(CFStringRef cfstr, char *buf, size_t buf_size)
{
    if (!cfstr || !buf || buf_size == 0) return;
    buf[0] = '\0';
    CFStringGetCString(cfstr, buf, (CFIndex)buf_size, kCFStringEncodingUTF8);
}

/* Helper to get integer property from IOHIDDeviceRef */
static long get_int_property(IOHIDDeviceRef dev, CFStringRef prop_key)
{
    CFTypeRef ref = IOHIDDeviceGetProperty(dev, prop_key);
    if (!ref || CFGetTypeID(ref) != CFNumberGetTypeID()) return 0;
    long val = 0;
    CFNumberGetValue((CFNumberRef)ref, kCFNumberLongType, &val);
    return val;
}

static void on_input_report(
    void *context,
    IOReturn result,
    void *sender,
    IOHIDReportType type,
    uint32_t report_id,
    uint8_t *report,
    CFIndex report_len)
{
    (void)result;
    (void)sender;
    (void)type;

    hid_device_t *dev = (hid_device_t *)context;
    if (!dev || !report || report_len < 3) {
        return;
    }

    hidpp_raw_report_t raw;
    memset(&raw, 0, sizeof(raw));

    /* In Apple IOKit, if numbered reports are used, the buffer passed to callback
     * may either contain the report_id at report[0] or omit it. If omitted, prepend it. */
    if (report_len > 0 && report[0] == (uint8_t)report_id) {
        raw.length = (size_t)report_len < sizeof(raw.data) ? (size_t)report_len : sizeof(raw.data);
        memcpy(raw.data, report, raw.length);
    } else {
        raw.data[0] = (uint8_t)report_id;
        size_t to_copy = (size_t)report_len < (sizeof(raw.data) - 1) ? (size_t)report_len : (sizeof(raw.data) - 1);
        memcpy(&raw.data[1], report, to_copy);
        raw.length = to_copy + 1;
    }

    /* Check if pending synchronous transaction is waiting for this message */
    if (dev->has_pending_tx && !dev->pending_resp_ready) {
        hidpp_msg_t incoming;
        if (hidpp_unpack(&raw, &incoming)) {
            bool matches = false;
            if (incoming.proto == HIDPP_PROTO_V20) {
                matches = hidpp_v20_matches_request(&dev->pending_req, &incoming.u.v20);
            } else if (incoming.proto == HIDPP_PROTO_V10) {
                /* Check for v10 error answering our request */
                const hidpp_v10_msg_t *v10 = &incoming.u.v10;
                if (v10->device_index == dev->pending_req.device_index &&
                    v10->sub_id == HIDPP_V10_SUB_ID_ERROR &&
                    v10->payload_len >= 3 &&
                    v10->payload[0] == dev->pending_req.feature_index) {
                    matches = true;
                }
            }

            if (matches) {
                dev->pending_resp = incoming;
                dev->pending_resp_ready = true;
            }
        }
    }

    /* Deliver to asynchronous report listener if registered */
    if (dev->report_cb) {
        dev->report_cb(dev, &raw, dev->report_user_data);
    }
}

static void populate_device_info(IOHIDDeviceRef dev_ref, hid_device_info_t *out_info)
{
    memset(out_info, 0, sizeof(*out_info));
    out_info->vendor_id = (uint16_t)get_int_property(dev_ref, CFSTR(kIOHIDVendorIDKey));
    out_info->product_id = (uint16_t)get_int_property(dev_ref, CFSTR(kIOHIDProductIDKey));

    long page = get_int_property(dev_ref, CFSTR(kIOHIDPrimaryUsagePageKey));
    if (page == 0) page = get_int_property(dev_ref, CFSTR(kIOHIDDeviceUsagePageKey));
    long usage = get_int_property(dev_ref, CFSTR(kIOHIDPrimaryUsageKey));
    if (usage == 0) usage = get_int_property(dev_ref, CFSTR(kIOHIDDeviceUsageKey));

    /* If primary usage properties returned 0, inspect DeviceUsagePairs array */
    if (page == 0 || usage == 0) {
        CFTypeRef pairs = IOHIDDeviceGetProperty(dev_ref, CFSTR(kIOHIDDeviceUsagePairsKey));
        if (pairs && CFGetTypeID(pairs) == CFArrayGetTypeID() && CFArrayGetCount((CFArrayRef)pairs) > 0) {
            CFDictionaryRef first = (CFDictionaryRef)CFArrayGetValueAtIndex((CFArrayRef)pairs, 0);
            if (first && CFGetTypeID(first) == CFDictionaryGetTypeID()) {
                CFNumberRef p_ref = (CFNumberRef)CFDictionaryGetValue(first, CFSTR(kIOHIDDeviceUsagePageKey));
                CFNumberRef u_ref = (CFNumberRef)CFDictionaryGetValue(first, CFSTR(kIOHIDDeviceUsageKey));
                if (p_ref) CFNumberGetValue(p_ref, kCFNumberLongType, &page);
                if (u_ref) CFNumberGetValue(u_ref, kCFNumberLongType, &usage);
            }
        }
    }

    out_info->usage_page = (uint16_t)page;
    out_info->usage = (uint16_t)usage;

    /* Check matching collections */
    for (size_t i = 0; i < NUM_COLLECTIONS; i++) {
        if (G_COLLECTIONS[i].page == out_info->usage_page &&
            G_COLLECTIONS[i].usage == out_info->usage) {
            out_info->is_long_only = G_COLLECTIONS[i].long_only;
            break;
        }
    }

    /* On macOS, Bluetooth devices (PID 0xBxxx) are always long-only over HID++ */
    if ((out_info->product_id & 0xF000) == 0xB000 || out_info->usage_page == 0xFF43) {
        out_info->is_long_only = true;
    }

    CFStringRef prod = (CFStringRef)IOHIDDeviceGetProperty(dev_ref, CFSTR(kIOHIDProductKey));
    if (prod) cfstring_to_cstr(prod, out_info->product_name, sizeof(out_info->product_name));

    CFStringRef serial = (CFStringRef)IOHIDDeviceGetProperty(dev_ref, CFSTR(kIOHIDSerialNumberKey));
    if (serial) cfstring_to_cstr(serial, out_info->serial_number, sizeof(out_info->serial_number));

    snprintf(out_info->transport_path, sizeof(out_info->transport_path), "iokit:%p", (void *)dev_ref);
}

static void on_device_matching(void *context, IOReturn result, void *sender, IOHIDDeviceRef dev_ref)
{
    (void)context;
    (void)result;
    (void)sender;

    if (g_device_count >= MAX_CACHED_DEVICES) return;

    /* Check if already present */
    for (size_t i = 0; i < g_device_count; i++) {
        if (g_devices[i] && g_devices[i]->io_device == dev_ref) {
            return;
        }
    }

    /* Ensure device is opened */
    IOHIDDeviceOpen(dev_ref, kIOHIDOptionsTypeNone);

    hid_device_t *dev = (hid_device_t *)calloc(1, sizeof(hid_device_t));
    if (!dev) return;

    dev->backend_type = HID_BACKEND_IOKIT;
    dev->io_device = dev_ref;
    populate_device_info(dev_ref, &dev->info);

    /* Formally associate device with RunLoop for asynchronous report delivery */
    IOHIDDeviceScheduleWithRunLoop(dev_ref, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    IOHIDDeviceRegisterInputReportCallback(
        dev_ref,
        dev->input_buffer,
        sizeof(dev->input_buffer),
        on_input_report,
        dev
    );

    g_devices[g_device_count++] = dev;

    if (g_hotplug_cb) {
        g_hotplug_cb(&dev->info, true, g_hotplug_user_data);
    }
}

static void on_device_removal(void *context, IOReturn result, void *sender, IOHIDDeviceRef dev_ref)
{
    (void)context;
    (void)result;
    (void)sender;

    for (size_t i = 0; i < g_device_count; i++) {
        if (g_devices[i] && g_devices[i]->io_device == dev_ref) {
            hid_device_t *dev = g_devices[i];
            if (g_hotplug_cb) {
                g_hotplug_cb(&dev->info, false, g_hotplug_user_data);
            }

            IOHIDDeviceRegisterInputReportCallback(dev_ref, dev->input_buffer, sizeof(dev->input_buffer), NULL, NULL);
            IOHIDDeviceUnscheduleFromRunLoop(dev_ref, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

            /* Shift remaining devices */
            for (size_t j = i; j + 1 < g_device_count; j++) {
                g_devices[j] = g_devices[j + 1];
            }
            g_devices[--g_device_count] = NULL;
            free(dev);
            break;
        }
    }
}

static CFMutableArrayRef create_matching_array(void)
{
    CFMutableArrayRef array = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    if (!array) return NULL;

    int vendor_id = LOGITECH_VENDOR_ID;
    CFNumberRef cf_vid = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor_id);

    /* 1. Add specific collection matches for DeviceUsagePage / DeviceUsage */
    for (size_t i = 0; i < NUM_COLLECTIONS; i++) {
        CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 3,
                                                                &kCFTypeDictionaryKeyCallBacks,
                                                                &kCFTypeDictionaryValueCallBacks);
        if (!dict) continue;

        int page = G_COLLECTIONS[i].page;
        int usage = G_COLLECTIONS[i].usage;
        CFNumberRef cf_page = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page);
        CFNumberRef cf_usage = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);

        CFDictionarySetValue(dict, CFSTR(kIOHIDVendorIDKey), cf_vid);
        CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsagePageKey), cf_page);
        CFDictionarySetValue(dict, CFSTR(kIOHIDDeviceUsageKey), cf_usage);
        CFArrayAppendValue(array, dict);
        CFRelease(dict);

        /* Also add PrimaryUsagePage / PrimaryUsage matching */
        CFMutableDictionaryRef dict_p = CFDictionaryCreateMutable(kCFAllocatorDefault, 3,
                                                                  &kCFTypeDictionaryKeyCallBacks,
                                                                  &kCFTypeDictionaryValueCallBacks);
        if (dict_p) {
            CFDictionarySetValue(dict_p, CFSTR(kIOHIDVendorIDKey), cf_vid);
            CFDictionarySetValue(dict_p, CFSTR(kIOHIDPrimaryUsagePageKey), cf_page);
            CFDictionarySetValue(dict_p, CFSTR(kIOHIDPrimaryUsageKey), cf_usage);
            CFArrayAppendValue(array, dict_p);
            CFRelease(dict_p);
        }

        CFRelease(cf_page);
        CFRelease(cf_usage);
    }

    /* 2. Generic Logitech VID match */
    CFMutableDictionaryRef vid_only = CFDictionaryCreateMutable(kCFAllocatorDefault, 1,
                                                                &kCFTypeDictionaryKeyCallBacks,
                                                                &kCFTypeDictionaryValueCallBacks);
    if (vid_only) {
        CFDictionarySetValue(vid_only, CFSTR(kIOHIDVendorIDKey), cf_vid);
        CFArrayAppendValue(array, vid_only);
        CFRelease(vid_only);
    }

    CFRelease(cf_vid);
    return array;
}

int hid_transport_init(void)
{
    if (g_hid_manager) return 0;

    g_hid_manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!g_hid_manager) {
        return -1;
    }

    CFMutableArrayRef matching = create_matching_array();
    if (matching) {
        IOHIDManagerSetDeviceMatchingMultiple(g_hid_manager, matching);
        CFRelease(matching);
    }

    IOHIDManagerRegisterDeviceMatchingCallback(g_hid_manager, on_device_matching, NULL);
    IOHIDManagerRegisterDeviceRemovalCallback(g_hid_manager, on_device_removal, NULL);

    IOHIDManagerScheduleWithRunLoop(g_hid_manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    IOReturn ret = IOHIDManagerOpen(g_hid_manager, kIOHIDOptionsTypeNone);
    if (ret != kIOReturnSuccess) {
        /* Open failed - likely Accessibility or Input Monitoring permission denied */
        return -2;
    }

    /* Populate existing devices immediately using IOHIDManagerCopyDevices */
    CFSetRef dev_set = IOHIDManagerCopyDevices(g_hid_manager);
    if (dev_set) {
        CFIndex dev_count = CFSetGetCount(dev_set);
        if (dev_count > 0) {
            IOHIDDeviceRef *devs = (IOHIDDeviceRef *)malloc(sizeof(IOHIDDeviceRef) * (size_t)dev_count);
            if (devs) {
                CFSetGetValues(dev_set, (const void **)devs);
                for (CFIndex i = 0; i < dev_count; i++) {
                    on_device_matching(NULL, kIOReturnSuccess, NULL, devs[i]);
                }
                free(devs);
            }
        }
        CFRelease(dev_set);
    }

    /* Pump runloop briefly to discover already-connected devices */
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);

    return 0;
}

void hid_transport_shutdown(void)
{
    if (!g_hid_manager) return;

    IOHIDManagerUnscheduleFromRunLoop(g_hid_manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDManagerClose(g_hid_manager, kIOHIDOptionsTypeNone);
    CFRelease(g_hid_manager);
    g_hid_manager = NULL;

    for (size_t i = 0; i < g_device_count; i++) {
        if (g_devices[i]) {
            if (g_devices[i]->io_device) {
                IOHIDDeviceRegisterInputReportCallback(
                    g_devices[i]->io_device,
                    g_devices[i]->input_buffer,
                    sizeof(g_devices[i]->input_buffer),
                    NULL,
                    NULL
                );
                IOHIDDeviceUnscheduleFromRunLoop(g_devices[i]->io_device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            }
            free(g_devices[i]);
            g_devices[i] = NULL;
        }
    }
    g_device_count = 0;
}

int hid_enumerate(hid_device_info_t *out_devices, size_t max_count)
{
    if (!out_devices || max_count == 0) return 0;
    if (!g_hid_manager) {
        int init_res = hid_transport_init();
        if (init_res < 0) return init_res;
    }

    /* Drain runloop to ensure current device snapshot */
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);

    size_t count = g_device_count < max_count ? g_device_count : max_count;
    for (size_t i = 0; i < count; i++) {
        if (g_devices[i]) {
            out_devices[i] = g_devices[i]->info;
        }
    }

    return (int)count;
}

hid_device_t *hid_open(const char *transport_path)
{
    if (!transport_path) return NULL;
    if (!g_hid_manager) hid_transport_init();

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);

    for (size_t i = 0; i < g_device_count; i++) {
        if (g_devices[i] && strcmp(g_devices[i]->info.transport_path, transport_path) == 0) {
            return g_devices[i];
        }
    }

    return NULL;
}

void hid_close(hid_device_t *dev)
{
    if (dev && dev->backend_type == HID_BACKEND_MOCK) {
        free(dev);
        return;
    }
    /* Real IOKit handles are managed by manager teardown */
}

const hid_device_info_t *hid_device_get_info(const hid_device_t *dev)
{
    return dev ? &dev->info : NULL;
}

bool hid_send_report(hid_device_t *dev, const hidpp_raw_report_t *report)
{
    if (!dev || !report || report->length == 0) {
        return false;
    }

    if (dev->backend_type == HID_BACKEND_MOCK) {
        return true;
    }

    if (!dev->io_device) {
        return false;
    }

    /* 1. Try sending with full report buffer (Report ID as data[0]) */
    IOReturn ret = IOHIDDeviceSetReport(
        dev->io_device,
        kIOHIDReportTypeOutput,
        report->data[0], /* Report ID */
        report->data,
        (CFIndex)report->length
    );

    /* 2. If rejected by OS, try sending without leading Report ID byte in buffer */
    if (ret != kIOReturnSuccess && report->length > 1) {
        ret = IOHIDDeviceSetReport(
            dev->io_device,
            kIOHIDReportTypeOutput,
            report->data[0],
            &report->data[1],
            (CFIndex)(report->length - 1)
        );
    }

    /* 3. If Output report failed, try Feature report with full buffer */
    if (ret != kIOReturnSuccess) {
        ret = IOHIDDeviceSetReport(
            dev->io_device,
            kIOHIDReportTypeFeature,
            report->data[0],
            report->data,
            (CFIndex)report->length
        );
    }

    return (ret == kIOReturnSuccess);
}

static uint64_t get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

/* Extern declaration for mock processor */
bool hid_mock_process_transaction(
    hid_device_t *dev,
    const hidpp_v20_msg_t *req,
    hidpp_msg_t *out_resp
);

bool hid_transact(
    hid_device_t *dev,
    const hidpp_v20_msg_t *req,
    hidpp_msg_t *out_resp,
    uint32_t timeout_ms)
{
    if (!dev || !req || !out_resp) {
        return false;
    }

    if (dev->backend_type == HID_BACKEND_MOCK) {
        return hid_mock_process_transaction(dev, req, out_resp);
    }

    /* Pack request into raw report */
    hidpp_raw_report_t raw_req;
    hidpp_v20_msg_t outbound = *req;

    /* Up-convert short message to long if device is long-only or Bluetooth (0xBxxx) */
    if ((dev->info.is_long_only || (dev->info.product_id & 0xF000) == 0xB000)
        && outbound.payload_len <= HIDPP_V20_SHORT_PAYLOAD_LEN) {
        outbound.report_id = HIDPP_REPORT_LONG;
    }

    if (!hidpp_v20_pack(&outbound, &raw_req)) {
        return false;
    }

    /* Set transaction wait barrier */
    dev->has_pending_tx = true;
    dev->pending_req = outbound;
    dev->pending_resp_ready = false;

    if (!hid_send_report(dev, &raw_req)) {
        dev->has_pending_tx = false;
        return false;
    }

    /* Wait for response by pumping CFRunLoop with small steps */
    uint64_t start_time = get_time_ms();
    while (!dev->pending_resp_ready) {
        uint64_t elapsed = get_time_ms() - start_time;
        if (elapsed >= timeout_ms) {
            dev->has_pending_tx = false;
            return false; /* Timed out */
        }

        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
    }

    *out_resp = dev->pending_resp;
    dev->has_pending_tx = false;
    return true;
}

bool hid_register_report_callback(
    hid_device_t *dev,
    hid_report_callback_t cb,
    void *user_data)
{
    if (!dev) return false;
    dev->report_cb = cb;
    dev->report_user_data = user_data;
    return true;
}

bool hid_start_hotplug_monitor(hid_hotplug_callback_t cb, void *user_data)
{
    g_hotplug_cb = cb;
    g_hotplug_user_data = user_data;
    return (hid_transport_init() == 0);
}

void hid_stop_hotplug_monitor(void)
{
    g_hotplug_cb = NULL;
    g_hotplug_user_data = NULL;
}

#endif /* __APPLE__ */
