/**
 * @file openlogi_discover.c
 * @brief Standalone diagnostic hardware discovery tool for Logitech peripherals.
 */

#include "device.h"
#include "hidpp.h"
#include "hid_transport.h"
#include "subcommands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int discover_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("=====================================================\n");
    printf("OpenLogi Pure C Hardware Discovery Tool\n");
    printf("=====================================================\n");

    int ret = hid_transport_init();
    if (ret != 0) {
        if (ret == -2) {
            fprintf(stderr, "[!] Warning: macOS Input Monitoring permission required.\n");
            fprintf(stderr, "    Please grant Input Monitoring to your terminal in:\n");
            fprintf(stderr, "    System Settings -> Privacy & Security -> Input Monitoring\n\n");
        } else {
            fprintf(stderr, "[!] Failed to initialize HID transport (error: %d)\n", ret);
        }
    }

    hid_device_info_t devices[16];
    int count = hid_enumerate(devices, 16);

    if (count <= 0) {
        printf("[-] No Logitech HID++ devices currently detected.\n");
        printf("    • Ensure your Logitech device (MX Master, MX Keys, etc.) is powered on.\n");
        printf("    • If using Logi Bolt / Unifying, ensure the USB receiver is plugged in.\n");
        printf("    • If using Bluetooth, ensure the mouse is connected in macOS Bluetooth settings.\n");
        hid_transport_shutdown();
        return 0;
    }

    printf("[+] Found %d Logitech HID++ device collection(s):\n\n", count);

    for (int i = 0; i < count; i++) {
        const hid_device_info_t *info = &devices[i];
        printf("-----------------------------------------------------\n");
        printf("[%d] Device Node:\n", i + 1);
        printf("    Product Name:   %s\n", info->product_name[0] ? info->product_name : "(unknown)");
        printf("    Vendor ID:      0x%04X (Logitech)\n", info->vendor_id);
        printf("    Product ID:     0x%04X\n", info->product_id);
        printf("    Usage Page/ID:  0x%04X / 0x%04X %s\n",
               info->usage_page, info->usage, info->is_long_only ? "(BLE Direct, Long-only)" : "");
        printf("    Serial Number:  %s\n", info->serial_number[0] ? info->serial_number : "(none)");
        printf("    Transport Path: %s\n", info->transport_path);

        /* Check if device with this serial number was already probed as a primary node */
        if (info->serial_number[0] != '\0') {
            bool already = false;
            for (int prev = 0; prev < i; prev++) {
                if (strcmp(devices[prev].serial_number, info->serial_number) == 0) {
                    already = true;
                    break;
                }
            }
            if (already) {
                printf("    [i] Secondary interface collection for serial %s (already probed above).\n", info->serial_number);
                continue;
            }
        }

        hid_device_t *dev = hid_open(info->transport_path);
        if (!dev) {
            printf("    [!] Could not open device endpoint for HID++ probing.\n");
            continue;
        }

        bool found_any = false;
        openlogi_device_t probed;

        /* Probe Device Index 0xFF (direct / receiver root) */
        if (openlogi_probe_device(dev, HIDPP_DEVICE_RECEIVER, HIDPP_DEFAULT_SW_ID, &probed)) {
            found_any = true;
            printf("\n    --> HID++ Probed Details (Device Index 0xFF):\n");
            printf("        Firmware Name: %s\n", probed.name[0] ? probed.name : "(none)");
            printf("        Protocol:      %s\n",
                   probed.proto_version == HIDPP_PROTO_V20 ? "HID++ 2.0+" : "HID++ 1.0");

            if (probed.feat_battery != 0) {
                printf("        Battery Level: %d%% (%s)\n",
                       probed.battery_percent, probed.battery_charging ? "Charging" : "Discharging");
            }

            if (probed.feat_dpi != 0) {
                printf("        Sensor DPI:    %d (Range: %d..%d, step: %d)\n",
                       probed.current_dpi, probed.min_dpi, probed.max_dpi, probed.dpi_step);
            }

            if (probed.feat_smartshift != 0) {
                const char *mode_str = "Unknown";
                if (probed.smartshift_mode == SMARTSHIFT_MODE_AUTO) mode_str = "Auto (SmartShift)";
                else if (probed.smartshift_mode == SMARTSHIFT_MODE_RATCHET) mode_str = "Ratchet (Clicky)";
                else if (probed.smartshift_mode == SMARTSHIFT_MODE_FREESPIN) mode_str = "Free-Spin";
                printf("        SmartShift:    Mode: %s | Torque Threshold: %d\n",
                       mode_str, probed.smartshift_threshold);
            }

            if (probed.feat_reprog_controls != 0) {
                printf("        ReprogControls: Supported (Feature Index: 0x%02X)\n",
                       probed.feat_reprog_controls);
            }
        }

        /* Only probe receiver slots 1..6 if index 0xFF was not a standalone device,
         * or if this device is specifically a USB receiver (Bolt/Unifying) */
        bool is_receiver = (info->product_id == 0xC52B || info->product_id == 0xC548 ||
                            info->product_id == 0xC539 || strstr(info->product_name, "Receiver") != NULL);
        if (!found_any || is_receiver) {
            for (uint8_t slot = 1; slot <= 6; slot++) {
            openlogi_device_t slot_dev;
            if (openlogi_probe_device(dev, slot, HIDPP_DEFAULT_SW_ID, &slot_dev)) {
                found_any = true;
                printf("\n    --> HID++ Probed Details (Receiver Slot %d):\n", slot);
                printf("        Firmware Name: %s\n", slot_dev.name[0] ? slot_dev.name : "(none)");
                printf("        Protocol:      %s\n",
                       slot_dev.proto_version == HIDPP_PROTO_V20 ? "HID++ 2.0+" : "HID++ 1.0");

                if (slot_dev.feat_battery != 0) {
                    printf("        Battery Level: %d%% (%s)\n",
                           slot_dev.battery_percent, slot_dev.battery_charging ? "Charging" : "Discharging");
                }

                if (slot_dev.feat_dpi != 0) {
                    printf("        Sensor DPI:    %d (Range: %d..%d, step: %d)\n",
                           slot_dev.current_dpi, slot_dev.min_dpi, slot_dev.max_dpi, slot_dev.dpi_step);
                }

                if (slot_dev.feat_smartshift != 0) {
                    const char *mode_str = "Unknown";
                    if (slot_dev.smartshift_mode == SMARTSHIFT_MODE_AUTO) mode_str = "Auto (SmartShift)";
                    else if (slot_dev.smartshift_mode == SMARTSHIFT_MODE_RATCHET) mode_str = "Ratchet (Clicky)";
                    else if (slot_dev.smartshift_mode == SMARTSHIFT_MODE_FREESPIN) mode_str = "Free-Spin";
                    printf("        SmartShift:    Mode: %s | Torque Threshold: %d\n",
                           mode_str, slot_dev.smartshift_threshold);
                }

                if (slot_dev.feat_reprog_controls != 0) {
                    printf("        ReprogControls: Supported (Feature Index: 0x%02X)\n",
                           slot_dev.feat_reprog_controls);
                }
            }
        }
        }

        if (!found_any) {
            printf("    [i] Device did not answer HID++ ping at index 0xFF or receiver slots 1..6.\n");
        }

        hid_close(dev);
    }

    printf("=====================================================\n");
    hid_transport_shutdown();
    return 0;
}
