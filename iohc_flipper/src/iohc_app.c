#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/text_input.h>
#include <gui/modules/dialog_ex.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <furi_hal_rtc.h>
#include <storage/storage.h>

#include "phy/phy_rx.h"
#include "log/log.h"
#include "ui/view_sniffer.h"
#include "ui/view_main_menu.h"
#include "ui/view_capture.h"
#include "ui/view_device_list.h"
#include "ui/view_device_actions.h"
#include "ui/view_identity.h"
#include "radio/radio_cc1101_regs.h"
#include "iohc/frame_parse.h"
#include "state/identity.h"
#include "state/device_book.h"
#include "state/tx_runner.h"

enum {
    VIEW_MAIN = 0,
    VIEW_SNIFFER,
    VIEW_CAPTURE,
    VIEW_DEVICES,
    VIEW_ACTIONS,
    VIEW_NAME_INPUT,
    VIEW_ALL_ACTIONS,
    VIEW_CONFIRM_DELETE,
    VIEW_IDENTITY,
};

typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    ViewDispatcher* view_dispatcher;
    IohcMainMenu* main_menu;
    IohcSniffView* sniff_view;
    IohcCaptureView* capture_view;
    IohcDeviceList* device_list;
    IohcDeviceActions* device_actions;
    IohcDeviceActions* all_actions;
    TextInput* name_input;
    char name_buf[IOHC_DEVICE_NAME_MAX + 1];
    DialogEx* confirm_dialog;
    char confirm_text[40];
    IohcIdentityView* identity_view;
    IohcPhy* phy;
    IohcLog* log;
    IohcIdentity identity;
    IohcDeviceBook* book;
    uint8_t pending_addr[3];        // address being saved
    uint8_t pending_vendor;         // raw payload[1] observed for that address
    uint8_t selected_device_index;  // currently selected in actions view
    FuriThread* consumer;
    volatile bool running;

    FuriMutex* mtx;
    bool last_parsed_valid;
    uint8_t last_src[3];
    volatile bool lock_active;
    uint8_t lock_src[3];
} IohcApp;


// ---------- consumer thread ----------

static int32_t consumer_thread(void* ctx) {
    IohcApp* app = (IohcApp*)ctx;
    IohcCapturedFrame f;
    while(app->running) {
        if(iohc_phy_wait_frame(app->phy, &f, 200)) {
            iohc_log_write(app->log, &f);
            IohcParsedFrame p;
            bool parsed_ok = iohc_frame_parse(f.bytes, f.len, &p);

            if(parsed_ok) {
                furi_mutex_acquire(app->mtx, FuriWaitForever);
                memcpy(app->last_src, p.src, 3);
                app->last_parsed_valid = true;
                furi_mutex_release(app->mtx);

                // Feed the capture view. Vendor byte at payload[1] is only
                // meaningful on button frames (cmd 0x00); pass cmd so the
                // view can decide whether to trust payload_byte_1.
                uint8_t payload_byte_1 = (p.payload_len >= 2) ? p.payload[1] : 0;
                iohc_capture_view_observe(
                    app->capture_view, p.src, p.dst, f.rssi_dbm,
                    p.cmdid, payload_byte_1);
            }

            bool lock_active;
            uint8_t lock_src[3];
            furi_mutex_acquire(app->mtx, FuriWaitForever);
            lock_active = app->lock_active;
            memcpy(lock_src, app->lock_src, 3);
            furi_mutex_release(app->mtx);

            const bool passes_lock =
                !lock_active || (parsed_ok && memcmp(p.src, lock_src, 3) == 0);

            iohc_sniff_view_update(
                app->sniff_view,
                iohc_phy_stat_frames_total(app->phy),
                iohc_phy_stat_framing_errors(app->phy),
                passes_lock ? &f : NULL,
                (parsed_ok && passes_lock) ? &p : NULL,
                lock_active, lock_src);
        } else {
            bool lock_active;
            uint8_t lock_src[3];
            furi_mutex_acquire(app->mtx, FuriWaitForever);
            lock_active = app->lock_active;
            memcpy(lock_src, app->lock_src, 3);
            furi_mutex_release(app->mtx);
            iohc_sniff_view_update(
                app->sniff_view,
                iohc_phy_stat_frames_total(app->phy),
                iohc_phy_stat_framing_errors(app->phy),
                NULL, NULL, lock_active, lock_src);
        }
    }
    return 0;
}

// ---------- navigation ----------

static bool nav_callback(void* ctx) {
    IohcApp* app = (IohcApp*)ctx;
    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static bool custom_event_callback(void* ctx, uint32_t event) {
    IohcApp* app = (IohcApp*)ctx;
    view_dispatcher_switch_to_view(app->view_dispatcher, event);
    return true;
}

// Per-view Back-navigation. Returning the parent view ID makes Back walk up
// the hierarchy instead of falling through to nav_callback (which exits).
static uint32_t back_to_main(void* ctx)     { UNUSED(ctx); return VIEW_MAIN; }
static uint32_t back_to_devices(void* ctx)  { UNUSED(ctx); return VIEW_DEVICES; }
static uint32_t back_to_capture(void* ctx)  { UNUSED(ctx); return VIEW_CAPTURE; }
static uint32_t back_to_actions(void* ctx)  { UNUSED(ctx); return VIEW_ACTIONS; }

// ---------- callbacks ----------

static void on_sniff_lock_toggle(void* ctx) {
    IohcApp* app = (IohcApp*)ctx;
    furi_mutex_acquire(app->mtx, FuriWaitForever);
    if(app->lock_active) {
        app->lock_active = false;
    } else if(app->last_parsed_valid) {
        memcpy(app->lock_src, app->last_src, 3);
        app->lock_active = true;
    }
    furi_mutex_release(app->mtx);
}

static void on_sniff_open_tx(void* ctx) {
    IohcApp* app = (IohcApp*)ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_MAIN);
}

static void on_name_input_done(void* ctx) {
    IohcApp* app = (IohcApp*)ctx;
    // man_id defaults from the vendor byte we captured. If we ever sniff a
    // cmd 0x30 frame matching this src, capture view will surface the real
    // wire byte instead.
    uint8_t man_id = iohc_man_id_default_for_vendor(app->pending_vendor);
    iohc_device_book_add(app->book, app->name_buf, app->pending_addr,
                         app->pending_vendor, man_id);
    iohc_device_book_save(app->book);
    iohc_device_list_refresh(app->device_list, app->book);
    notification_message(app->notifications, &sequence_success);
    view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_DEVICES);
}

static void on_capture_save(const uint8_t* addr, uint8_t vendor, void* ctx) {
    IohcApp* app = (IohcApp*)ctx;
    memcpy(app->pending_addr, addr, 3);
    app->pending_vendor = vendor;
    snprintf(app->name_buf, sizeof(app->name_buf), "Shutter %02X%02X%02X",
        addr[0], addr[1], addr[2]);
    text_input_set_header_text(app->name_input, "Name this shutter");
    text_input_set_result_callback(
        app->name_input, (TextInputCallback)on_name_input_done, app,
        app->name_buf, sizeof(app->name_buf), false);
    view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_NAME_INPUT);
}

static uint8_t btn_for(IohcDevAction action) {
    switch(action) {
        case IohcDevActionUp:   return IOHC_BTN_UP;
        case IohcDevActionDown: return IOHC_BTN_DOWN;
        case IohcDevActionStop: return IOHC_BTN_STOP;
        default:                return IOHC_BTN_STOP;
    }
}

// Per-device action: use that shutter's own identity for src/key/seq.
static void perform_device_action(IohcApp* app, IohcDevAction action) {
    notification_message(app->notifications, &sequence_blink_blue_100);
    bool ok;
    if(action == IohcDevActionPair) {
        ok = iohc_tx_send_pair_dev(app->book, app->selected_device_index);
    } else {
        ok = iohc_tx_send_button_dev(app->book, app->selected_device_index, btn_for(action));
    }
    notification_message(app->notifications, ok ? &sequence_blink_green_100 : &sequence_blink_red_100);
}

// "All shutters" action: use the FAP's global identity (Mode A — assumes
// the user has paired the global identity with the motors they want to
// blast). Future: iterate per-device identities for Mode B.
static void perform_global_action(IohcApp* app, IohcDevAction action) {
    notification_message(app->notifications, &sequence_blink_blue_100);
    bool ok;
    if(action == IohcDevActionPair) {
        ok = iohc_tx_send_pair_global(&app->identity);
    } else {
        ok = iohc_tx_send_button_global(&app->identity, btn_for(action));
    }
    notification_message(app->notifications, ok ? &sequence_blink_green_100 : &sequence_blink_red_100);
}

static void on_confirm_delete(DialogExResult result, void* ctx) {
    IohcApp* app = (IohcApp*)ctx;
    if(result == DialogExResultRight) {
        iohc_device_book_remove(app->book, app->selected_device_index);
        iohc_device_book_save(app->book);
        iohc_device_list_refresh(app->device_list, app->book);
        view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_DEVICES);
    } else {
        view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_ACTIONS);
    }
}

static void on_device_action(IohcDevAction action, void* ctx) {
    IohcApp* app = (IohcApp*)ctx;
    const IohcDevice* d = iohc_device_book_get(app->book, app->selected_device_index);
    if(!d) return;

    if(action == IohcDevActionDelete) {
        snprintf(app->confirm_text, sizeof(app->confirm_text), "Delete \"%s\"?", d->name);
        dialog_ex_set_header(app->confirm_dialog, app->confirm_text, 64, 8, AlignCenter, AlignTop);
        dialog_ex_set_text(app->confirm_dialog,
            "This removes the entry.\nPairing on motor stays.",
            64, 28, AlignCenter, AlignTop);
        dialog_ex_set_left_button_text(app->confirm_dialog, "Cancel");
        dialog_ex_set_right_button_text(app->confirm_dialog, "Delete");
        view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_CONFIRM_DELETE);
        return;
    }

    // Stay on the actions submenu for this shutter so the user can
    // hit multiple commands in a row without re-navigating.
    (void)d;  // d is consulted inside perform_device_action via app->book
    perform_device_action(app, action);
}

static void on_all_action(IohcDevAction action, void* ctx) {
    IohcApp* app = (IohcApp*)ctx;
    if(action == IohcDevActionDelete) return;  // n/a for broadcast
    perform_global_action(app, action);
    // Stay on the All-shutters menu after a broadcast.
}

static void on_device_selected(uint8_t index, void* ctx) {
    IohcApp* app = (IohcApp*)ctx;
    if(index >= iohc_device_book_count(app->book)) return;
    app->selected_device_index = index;
    const IohcDevice* d = iohc_device_book_get(app->book, index);
    char header[40];
    snprintf(header, sizeof(header), "%s (%02X%02X%02X)",
        d->name, d->motor_addr[0], d->motor_addr[1], d->motor_addr[2]);
    iohc_device_actions_set_label(app->device_actions, header);
    view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_ACTIONS);
}

static void on_main_menu(IohcMainMenuChoice choice, void* ctx) {
    IohcApp* app = (IohcApp*)ctx;
    switch(choice) {
        case IohcMainMenuCapture:
            iohc_capture_view_reset(app->capture_view);
            view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_CAPTURE);
            break;
        case IohcMainMenuDevices:
            iohc_device_list_refresh(app->device_list, app->book);
            view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_DEVICES);
            break;
        case IohcMainMenuAllShutters:
            iohc_device_actions_set_label(app->all_actions, "All shutters (broadcast)");
            view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_ALL_ACTIONS);
            break;
        case IohcMainMenuPair: {
            notification_message(app->notifications, &sequence_blink_blue_100);
            bool ok = iohc_tx_send_pair_global(&app->identity);
            notification_message(app->notifications, ok ? &sequence_blink_green_100 : &sequence_blink_red_100);
            break;
        }
        case IohcMainMenuSniffer:
            view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_SNIFFER);
            break;
        case IohcMainMenuIdentity:
            iohc_identity_view_set_identity(app->identity_view, &app->identity);
            view_dispatcher_send_custom_event(app->view_dispatcher, VIEW_IDENTITY);
            break;
    }
}

// Copy a file from src_path to dst_path on SD. Returns true on success.
static bool copy_file(Storage* storage, const char* src_path, const char* dst_path) {
    File* src = storage_file_alloc(storage);
    File* dst = storage_file_alloc(storage);
    bool ok = storage_file_open(src, src_path, FSAM_READ, FSOM_OPEN_EXISTING);
    if(ok) {
        ok = storage_file_open(dst, dst_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
        if(ok) {
            uint8_t buf[128];
            uint16_t n;
            while((n = storage_file_read(src, buf, sizeof(buf))) > 0) {
                storage_file_write(dst, buf, n);
            }
            storage_file_close(dst);
        }
        storage_file_close(src);
    }
    storage_file_free(src);
    storage_file_free(dst);
    return ok;
}

// Manual snapshot: copies identity.bin + devices.bin to timestamped paths and
// writes a JSON migration export. Used as the Home Assistant handover artifact
// — see project_flipper_temporary memo.
static void on_identity_backup(void* ctx) {
    IohcApp* app = (IohcApp*)ctx;

    DateTime now;
    furi_hal_rtc_get_datetime(&now);
    char stamp[24];
    snprintf(stamp, sizeof(stamp), "%04u%02u%02u-%02u%02u%02u",
        now.year, now.month, now.day, now.hour, now.minute, now.second);

    char id_dest[128], book_dest[128], json_dest[128];
    snprintf(id_dest, sizeof(id_dest),
        EXT_PATH("apps_data/iohc_flipper/identity-%s.bin"), stamp);
    snprintf(book_dest, sizeof(book_dest),
        EXT_PATH("apps_data/iohc_flipper/devices-%s.bin"), stamp);
    snprintf(json_dest, sizeof(json_dest),
        EXT_PATH("apps_data/iohc_flipper/export-%s.json"), stamp);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool id_ok   = copy_file(storage, EXT_PATH("apps_data/iohc_flipper/identity.bin"), id_dest);
    bool book_ok = copy_file(storage, EXT_PATH("apps_data/iohc_flipper/devices.bin"),  book_dest);
    furi_record_close(RECORD_STORAGE);

    bool json_ok = iohc_device_book_export_json(app->book, &app->identity, json_dest);

    bool all_ok = id_ok && json_ok;  // devices.bin may be absent if no devices saved yet
    (void)book_ok;

    iohc_identity_view_show_toast(app->identity_view,
        all_ok ? "Backup saved" : "Backup failed",
        stamp);
    notification_message(app->notifications,
        all_ok ? &sequence_blink_green_100 : &sequence_blink_red_100);
}

// ---------- entry point ----------

int32_t iohc_app_main(void* p) {
    UNUSED(p);
    IohcApp* app = malloc(sizeof(IohcApp));
    memset(app, 0, sizeof(*app));

    app->mtx = furi_mutex_alloc(FuriMutexTypeNormal);
    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    iohc_identity_load_or_generate(&app->identity);
    app->book = iohc_device_book_alloc();
    iohc_device_book_load(app->book);

    app->view_dispatcher = view_dispatcher_alloc();
    app->main_menu = iohc_main_menu_alloc(on_main_menu, app);
    app->sniff_view = iohc_sniff_view_alloc();
    iohc_sniff_view_set_lock_toggle(app->sniff_view, on_sniff_lock_toggle, app);
    iohc_sniff_view_set_open_tx(app->sniff_view, on_sniff_open_tx, app);
    app->capture_view = iohc_capture_view_alloc();
    iohc_capture_view_set_on_save(app->capture_view, on_capture_save, app);
    app->device_list = iohc_device_list_alloc();
    iohc_device_list_set_on_select(app->device_list, on_device_selected, app);
    app->device_actions = iohc_device_actions_alloc(on_device_action, app);
    app->all_actions = iohc_device_actions_alloc(on_all_action, app);
    app->name_input = text_input_alloc();
    app->confirm_dialog = dialog_ex_alloc();
    dialog_ex_set_result_callback(app->confirm_dialog, on_confirm_delete);
    dialog_ex_set_context(app->confirm_dialog, app);
    app->identity_view = iohc_identity_view_alloc();
    iohc_identity_view_set_on_backup(app->identity_view, on_identity_backup, app);

    app->phy = iohc_phy_alloc();
    app->log = iohc_log_alloc();

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, nav_callback);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, custom_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    View* v_main     = iohc_main_menu_get_view(app->main_menu);
    View* v_sniffer  = iohc_sniff_view_get_view(app->sniff_view);
    View* v_capture  = iohc_capture_view_get_view(app->capture_view);
    View* v_devices  = iohc_device_list_get_view(app->device_list);
    View* v_actions  = iohc_device_actions_get_view(app->device_actions);
    View* v_all      = iohc_device_actions_get_view(app->all_actions);
    View* v_name     = text_input_get_view(app->name_input);
    View* v_confirm  = dialog_ex_get_view(app->confirm_dialog);
    View* v_identity = iohc_identity_view_get_view(app->identity_view);

    // Back navigation hierarchy. Main view has no previous → dispatcher's
    // nav_callback fires and exits the app.
    view_set_previous_callback(v_sniffer,  back_to_main);
    view_set_previous_callback(v_capture,  back_to_main);
    view_set_previous_callback(v_devices,  back_to_main);
    view_set_previous_callback(v_actions,  back_to_devices);
    view_set_previous_callback(v_all,      back_to_main);
    view_set_previous_callback(v_name,     back_to_capture);
    view_set_previous_callback(v_confirm,  back_to_actions);
    view_set_previous_callback(v_identity, back_to_main);

    view_dispatcher_add_view(app->view_dispatcher, VIEW_MAIN,           v_main);
    view_dispatcher_add_view(app->view_dispatcher, VIEW_SNIFFER,        v_sniffer);
    view_dispatcher_add_view(app->view_dispatcher, VIEW_CAPTURE,        v_capture);
    view_dispatcher_add_view(app->view_dispatcher, VIEW_DEVICES,        v_devices);
    view_dispatcher_add_view(app->view_dispatcher, VIEW_ACTIONS,        v_actions);
    view_dispatcher_add_view(app->view_dispatcher, VIEW_ALL_ACTIONS,    v_all);
    view_dispatcher_add_view(app->view_dispatcher, VIEW_NAME_INPUT,     v_name);
    view_dispatcher_add_view(app->view_dispatcher, VIEW_CONFIRM_DELETE, v_confirm);
    view_dispatcher_add_view(app->view_dispatcher, VIEW_IDENTITY,       v_identity);

    view_dispatcher_switch_to_view(app->view_dispatcher, VIEW_MAIN);

    iohc_phy_start(app->phy, IOHC_FREQ_HZ_CH2);
    app->running = true;
    app->consumer = furi_thread_alloc_ex("iohc_consumer", 2048, consumer_thread, app);
    furi_thread_start(app->consumer);

    view_dispatcher_run(app->view_dispatcher);

    app->running = false;
    furi_thread_join(app->consumer);
    furi_thread_free(app->consumer);

    iohc_phy_stop(app->phy);
    iohc_phy_free(app->phy);
    iohc_log_free(app->log);
    iohc_device_book_free(app->book);

    view_dispatcher_remove_view(app->view_dispatcher, VIEW_MAIN);
    view_dispatcher_remove_view(app->view_dispatcher, VIEW_SNIFFER);
    view_dispatcher_remove_view(app->view_dispatcher, VIEW_CAPTURE);
    view_dispatcher_remove_view(app->view_dispatcher, VIEW_DEVICES);
    view_dispatcher_remove_view(app->view_dispatcher, VIEW_ACTIONS);
    view_dispatcher_remove_view(app->view_dispatcher, VIEW_ALL_ACTIONS);
    view_dispatcher_remove_view(app->view_dispatcher, VIEW_NAME_INPUT);
    view_dispatcher_remove_view(app->view_dispatcher, VIEW_CONFIRM_DELETE);
    view_dispatcher_remove_view(app->view_dispatcher, VIEW_IDENTITY);

    iohc_main_menu_free(app->main_menu);
    iohc_sniff_view_free(app->sniff_view);
    iohc_capture_view_free(app->capture_view);
    iohc_device_list_free(app->device_list);
    iohc_device_actions_free(app->device_actions);
    iohc_device_actions_free(app->all_actions);
    text_input_free(app->name_input);
    dialog_ex_free(app->confirm_dialog);
    iohc_identity_view_free(app->identity_view);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_mutex_free(app->mtx);
    free(app);
    return 0;
}
