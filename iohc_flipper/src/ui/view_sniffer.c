#include "view_sniffer.h"

#include <furi.h>
#include <gui/elements.h>
#include <stdio.h>

typedef struct {
    uint32_t frame_count;
    uint32_t errors;

    bool has_frame;
    int8_t last_rssi;
    uint8_t last_len;

    bool has_parsed;
    uint8_t src[3];
    uint8_t dst[3];
    uint8_t cmdid;
    uint8_t msg_len;
    bool one_way;
    bool crc_ok;

    bool lock_active;
    uint8_t lock_src[3];
} IohcSniffModel;

struct IohcSniffView {
    View* view;
    IohcSniffOnLockToggle on_lock_toggle;
    void* on_lock_ctx;
    IohcSniffOnOpenTx on_open_tx;
    void* on_open_tx_ctx;
};

static void draw_callback(Canvas* canvas, void* model) {
    IohcSniffModel* m = (IohcSniffModel*)model;
    char buf[40];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "iohc 868.95");

    canvas_set_font(canvas, FontSecondary);

    if(m->lock_active) {
        snprintf(buf, sizeof(buf), "L:%02X%02X%02X",
            m->lock_src[0], m->lock_src[1], m->lock_src[2]);
        canvas_draw_str(canvas, 80, 10, buf);
    }

    snprintf(buf, sizeof(buf), "#%lu err:%lu",
        (unsigned long)m->frame_count, (unsigned long)m->errors);
    canvas_draw_str(canvas, 2, 21, buf);

    if(m->has_frame) {
        snprintf(buf, sizeof(buf), "rssi:%d", (int)m->last_rssi);
        canvas_draw_str(canvas, 72, 21, buf);
    }

    if(m->has_parsed) {
        snprintf(buf, sizeof(buf), "src %02X%02X%02X cmd:%02X",
            m->src[0], m->src[1], m->src[2], m->cmdid);
        canvas_draw_str(canvas, 2, 32, buf);

        snprintf(buf, sizeof(buf), "dst %02X%02X%02X %s",
            m->dst[0], m->dst[1], m->dst[2], m->one_way ? "1W" : "2W");
        canvas_draw_str(canvas, 2, 43, buf);

        snprintf(buf, sizeof(buf), "len:%u CRC %s",
            (unsigned)m->msg_len, m->crc_ok ? "OK" : "BAD");
        canvas_draw_str(canvas, 2, 54, buf);
    } else if(!m->has_frame) {
        canvas_draw_str(canvas, 2, 32, "waiting for frames...");
    }

    elements_button_center(canvas, m->lock_active ? "Unlock" : "Lock");
    elements_button_right(canvas, "TX");
}

static bool input_callback(InputEvent* event, void* ctx) {
    IohcSniffView* v = (IohcSniffView*)ctx;
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(v->on_lock_toggle) v->on_lock_toggle(v->on_lock_ctx);
        return true;
    }
    if(event->type == InputTypeShort && event->key == InputKeyRight) {
        if(v->on_open_tx) v->on_open_tx(v->on_open_tx_ctx);
        return true;
    }
    return false;
}

IohcSniffView* iohc_sniff_view_alloc(void) {
    IohcSniffView* v = malloc(sizeof(IohcSniffView));
    v->view = view_alloc();
    v->on_lock_toggle = NULL;
    v->on_lock_ctx = NULL;
    v->on_open_tx = NULL;
    v->on_open_tx_ctx = NULL;
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(IohcSniffModel));
    view_set_draw_callback(v->view, draw_callback);
    view_set_input_callback(v->view, input_callback);
    view_set_context(v->view, v);
    return v;
}

void iohc_sniff_view_free(IohcSniffView* v) {
    view_free(v->view);
    free(v);
}

View* iohc_sniff_view_get_view(IohcSniffView* v) {
    return v->view;
}

void iohc_sniff_view_set_lock_toggle(
    IohcSniffView* v, IohcSniffOnLockToggle cb, void* ctx) {
    v->on_lock_toggle = cb;
    v->on_lock_ctx = ctx;
}

void iohc_sniff_view_set_open_tx(
    IohcSniffView* v, IohcSniffOnOpenTx cb, void* ctx) {
    v->on_open_tx = cb;
    v->on_open_tx_ctx = ctx;
}

void iohc_sniff_view_update(
    IohcSniffView* v,
    uint32_t frame_count,
    uint32_t errors,
    const IohcCapturedFrame* last,
    const IohcParsedFrame* parsed,
    bool lock_active,
    const uint8_t lock_src[3]) {
    with_view_model(
        v->view, IohcSniffModel* m,
        {
            m->frame_count = frame_count;
            m->errors = errors;
            if(last) {
                m->has_frame = true;
                m->last_rssi = last->rssi_dbm;
                m->last_len = last->len;
            }
            if(parsed) {
                m->has_parsed = true;
                memcpy(m->src, parsed->src, 3);
                memcpy(m->dst, parsed->dst, 3);
                m->cmdid = parsed->cmdid;
                m->msg_len = parsed->raw_len;
                m->one_way = parsed->one_way;
                m->crc_ok = parsed->crc_ok;
            }
            m->lock_active = lock_active;
            if(lock_active && lock_src) memcpy(m->lock_src, lock_src, 3);
        },
        true);
}
