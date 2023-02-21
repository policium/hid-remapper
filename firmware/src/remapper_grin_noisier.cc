#include "pico/stdlib.h"

#include <tusb.h>

#include "descriptor_parser.h"
#include "remapper.h"

#ifndef SOLENOID_PIN
#define SOLENOID_PIN               2
#endif
#define SOLENOID_STATE_ON          1

void extra_init() {
    gpio_init(SOLENOID_PIN);
    gpio_set_dir(SOLENOID_PIN, GPIO_OUT);
}

void solenoid(bool push) {
    static bool solenoid_state = false;
    static uint64_t turn_solenoid_off_after = 0;
    static uint64_t turn_solenoid_off_for = 0;

    if (push == solenoid_state) return;

    if (push && time_us_64() > turn_solenoid_off_for) {
        solenoid_state = true;
        gpio_put(SOLENOID_PIN, SOLENOID_STATE_ON);
        turn_solenoid_off_after = time_us_64() + 100000;
    } else if (!push && time_us_64() > turn_solenoid_off_after) {
        solenoid_state = false;
        gpio_put(SOLENOID_PIN, 1-SOLENOID_STATE_ON);
        turn_solenoid_off_for = time_us_64() + 10000;
    }
}

static bool reports_received = false;

bool read_report() {
    solenoid(false);

    tuh_task();

    bool ret = reports_received;
    reports_received = false;
    return ret;
}

void interval_override_updated() {
}

void flash_b_side() {
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    printf("tuh_hid_mount_cb\n");

    uint16_t vid;
    uint16_t pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    parse_descriptor(vid, pid, desc_report, desc_len, (uint16_t) (dev_addr << 8) | instance);

    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    printf("tuh_hid_umount_cb\n");
    clear_descriptor_data(dev_addr);
}

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode) {
    for (uint8_t i=0; i<6; i++) {
        if (report->keycode[i] == keycode)  return true;
    }

    return false;
}

// convert hid keycode to ascii and print via usb device CDC (ignore non-printable)
static void process_kbd_report(uint8_t dev_addr, hid_keyboard_report_t const *report) {
    static hid_keyboard_report_t prev_report = { 0, 0, {0} }; // previous report to check key released

    for (uint8_t i=0; i<6; i++) {
        uint8_t keycode = report->keycode[i];
        if (keycode) {
            if (find_key_in_report(&prev_report, keycode)) {
                // exist in previous report means the current key is holding
            } else {
                // not existed in previous report means the current key is pressed
                solenoid(true);
            }
        }
    }

    prev_report = *report;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    switch(itf_protocol)
    {
        case HID_ITF_PROTOCOL_NONE:
        case HID_ITF_PROTOCOL_KEYBOARD:
            process_kbd_report(dev_addr, (hid_keyboard_report_t const*) report );
            break;
        case HID_ITF_PROTOCOL_MOUSE:
            //process_mouse_report(dev_addr, (hid_mouse_report_t const*) report );
            break;
        default:
            break;
    }

    handle_received_report(report, len, (uint16_t) (dev_addr << 8) | instance);

    reports_received = true;

    tuh_hid_receive_report(dev_addr, instance);
}
