#include "view_capture.h"

#include <furi.h>
#include <gui/elements.h>
#include <input/input.h>
#include <stdio.h>
#include <string.h>

#define CAPTURE_MAX 8

// Convenience for the UI only. Anywhere we need to act on the byte we still
// pass through the raw value — this is purely a presentation label.
static const char* vendor_label(uint8_t v) {
    if(v == 0x43) return "Somfy";
    if(v == 0x61) return "Velux";
    return "?";
}

// One unique source address (i.e. one physical remote). Indexed by src, not
// dst, because a single Velux remote emits to 3 different product-type-code
// dsts per gear/rings press — and the *vendor* byte we care about lives in
// a different frame type (cmd 0x00 button frames). So we merge per-src.
typedef struct {
    uint8_t src[3];
    uint8_t motor_addr[3];  // first non-broadcast dst we saw from this src
    bool has_motor_addr;
    uint8_t vendor;         // payload[1] from a cmd 0x00 button frame
    bool has_vendor;
    int8_t rssi;
    uint16_t hits;
} CapturedSrc;

typedef struct {
    bool has_any;              // any frame seen
    uint32_t frame_count;
    CapturedSrc items[CAPTURE_MAX];
    uint8_t count;             // distinct srcs in items[]
    uint8_t cursor;            // index of currently-shown entry
} IohcCaptureModel;

struct IohcCaptureView {
    View* view;
    IohcCaptureOnSave on_save;
    void* on_save_ctx;
};

static void draw_callback(Canvas* canvas, void* model) {
    IohcCaptureModel* m = (IohcCaptureModel*)model;
    char buf[40];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(m->count > 0) {
        snprintf(buf, sizeof(buf), "Remote %u/%u",
            (unsigned)(m->cursor + 1), (unsigned)m->count);
        canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, buf);
    } else {
        canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, "Capture");
    }

    canvas_set_font(canvas, FontSecondary);

    if(m->count > 0) {
        const CapturedSrc* a = &m->items[m->cursor];
        snprintf(buf, sizeof(buf), "src %02X:%02X:%02X (#%u)",
            a->src[0], a->src[1], a->src[2], (unsigned)a->hits);
        canvas_draw_str(canvas, 6, 26, buf);
        if(a->has_vendor) {
            snprintf(buf, sizeof(buf), "vendor %s 0x%02X",
                vendor_label(a->vendor), (unsigned)a->vendor);
        } else {
            snprintf(buf, sizeof(buf), "vendor ? (press UP)");
        }
        canvas_draw_str(canvas, 6, 36, buf);
        snprintf(buf, sizeof(buf), "rssi %d dBm", (int)a->rssi);
        canvas_draw_str(canvas, 6, 46, buf);
        if(a->has_vendor) elements_button_center(canvas, "Save");
        if(m->count > 1) {
            elements_button_left(canvas, "Prev");
            elements_button_right(canvas, "Next");
        }
    } else if(m->has_any) {
        canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignTop, "Frames seen, but");
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignTop, "no usable remote yet.");
        snprintf(buf, sizeof(buf), "(%lu frames)", (unsigned long)m->frame_count);
        canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignTop, buf);
    } else {
        canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignTop, "Press remote");
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignTop, "near a shutter");
        canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignTop, "Waiting...");
    }
}

static bool input_callback(InputEvent* event, void* ctx) {
    IohcCaptureView* v = (IohcCaptureView*)ctx;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyOk) {
        bool has = false;
        uint8_t addr[3];
        uint8_t vendor = 0;
        with_view_model(
            v->view, IohcCaptureModel* m,
            {
                if(m->count > 0 && m->items[m->cursor].has_vendor) {
                    has = true;
                    const CapturedSrc* a = &m->items[m->cursor];
                    // motor_addr = first non-broadcast dst we saw; else src
                    // (just a label — TX always broadcasts).
                    if(a->has_motor_addr) memcpy(addr, a->motor_addr, 3);
                    else                  memcpy(addr, a->src, 3);
                    vendor = a->vendor;
                }
            },
            false);
        if(has && v->on_save) v->on_save(addr, vendor, v->on_save_ctx);
        return true;
    }
    if(event->key == InputKeyLeft) {
        with_view_model(
            v->view, IohcCaptureModel* m,
            {
                if(m->count > 1) m->cursor = (uint8_t)((m->cursor + m->count - 1) % m->count);
            },
            true);
        return true;
    }
    if(event->key == InputKeyRight) {
        with_view_model(
            v->view, IohcCaptureModel* m,
            {
                if(m->count > 1) m->cursor = (uint8_t)((m->cursor + 1) % m->count);
            },
            true);
        return true;
    }
    return false;
}

IohcCaptureView* iohc_capture_view_alloc(void) {
    IohcCaptureView* v = malloc(sizeof(IohcCaptureView));
    v->view = view_alloc();
    v->on_save = NULL;
    v->on_save_ctx = NULL;
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(IohcCaptureModel));
    view_set_draw_callback(v->view, draw_callback);
    view_set_input_callback(v->view, input_callback);
    view_set_context(v->view, v);
    return v;
}

void iohc_capture_view_free(IohcCaptureView* v) {
    view_free(v->view);
    free(v);
}

View* iohc_capture_view_get_view(IohcCaptureView* v) {
    return v->view;
}

void iohc_capture_view_set_on_save(IohcCaptureView* v, IohcCaptureOnSave cb, void* ctx) {
    v->on_save = cb;
    v->on_save_ctx = ctx;
}

void iohc_capture_view_reset(IohcCaptureView* v) {
    with_view_model(
        v->view, IohcCaptureModel* m,
        {
            m->has_any = false;
            m->frame_count = 0;
            m->count = 0;
            m->cursor = 0;
        },
        true);
}

void iohc_capture_view_observe(
    IohcCaptureView* v, const uint8_t src[3], const uint8_t dst[3],
    int8_t rssi, uint8_t cmd, uint8_t payload_byte_1) {
    const bool dst_is_broadcast =
        (dst[0] == 0x00 && dst[1] == 0x00 && dst[2] == 0x3F) ||
        (dst[0] == 0xFF && dst[1] == 0xFF && (dst[2] == 0xFE || dst[2] == 0xFF));

    with_view_model(
        v->view, IohcCaptureModel* m,
        {
            m->frame_count++;
            m->has_any = true;
            // Index by src.
            int found = -1;
            for(uint8_t i = 0; i < m->count; i++) {
                if(memcmp(m->items[i].src, src, 3) == 0) {
                    found = i;
                    break;
                }
            }
            CapturedSrc* a;
            if(found >= 0) {
                a = &m->items[found];
            } else if(m->count < CAPTURE_MAX) {
                a = &m->items[m->count];
                memset(a, 0, sizeof(*a));
                memcpy(a->src, src, 3);
                m->count++;
            } else {
                a = NULL;
            }
            if(a) {
                a->hits++;
                a->rssi = rssi;
                // Record the first non-broadcast dst as motor_addr label.
                if(!a->has_motor_addr && !dst_is_broadcast) {
                    memcpy(a->motor_addr, dst, 3);
                    a->has_motor_addr = true;
                }
                // Only trust payload[1] as the vendor byte on button frames.
                // cmd 0x2E/0x30/etc. have unrelated meanings at that offset.
                if(cmd == 0x00) {
                    a->vendor = payload_byte_1;
                    a->has_vendor = true;
                }
            }
        },
        true);
}
