#include "view_identity.h"

#include <furi.h>
#include <gui/elements.h>
#include <input/input.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    IohcIdentity id;
    bool has_id;
    char toast_line1[32];
    char toast_line2[32];
    uint32_t toast_until_tick;
} IohcIdentityModel;

struct IohcIdentityView {
    View* view;
    IohcIdentityOnBackup on_backup;
    void* on_backup_ctx;
};

static void draw_callback(Canvas* canvas, void* model) {
    IohcIdentityModel* m = (IohcIdentityModel*)model;
    char buf[40];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, "Identity");

    canvas_set_font(canvas, FontSecondary);

    if(m->has_id) {
        snprintf(buf, sizeof(buf), "src %02X:%02X:%02X",
            m->id.src_addr[0], m->id.src_addr[1], m->id.src_addr[2]);
        canvas_draw_str(canvas, 4, 18, buf);

        snprintf(buf, sizeof(buf), "key %02X%02X%02X%02X%02X%02X%02X%02X",
            m->id.install_key[0], m->id.install_key[1], m->id.install_key[2], m->id.install_key[3],
            m->id.install_key[4], m->id.install_key[5], m->id.install_key[6], m->id.install_key[7]);
        canvas_draw_str(canvas, 4, 30, buf);

        snprintf(buf, sizeof(buf), "    %02X%02X%02X%02X%02X%02X%02X%02X",
            m->id.install_key[8], m->id.install_key[9], m->id.install_key[10], m->id.install_key[11],
            m->id.install_key[12], m->id.install_key[13], m->id.install_key[14], m->id.install_key[15]);
        canvas_draw_str(canvas, 4, 40, buf);

        snprintf(buf, sizeof(buf), "seq 0x%04X", (unsigned)m->id.seq);
        canvas_draw_str(canvas, 4, 50, buf);
    }

    // Transient toast overlay (shown for ~2 s after backup).
    if(m->toast_until_tick && furi_get_tick() < m->toast_until_tick) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 2, 14, 124, 40);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, 2, 14, 124, 40, 4);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignTop, m->toast_line1);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignTop, m->toast_line2);
    } else {
        elements_button_center(canvas, "Backup");
    }
}

static bool input_callback(InputEvent* event, void* ctx) {
    IohcIdentityView* v = (IohcIdentityView*)ctx;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyOk) {
        if(v->on_backup) v->on_backup(v->on_backup_ctx);
        return true;
    }
    return false;
}

IohcIdentityView* iohc_identity_view_alloc(void) {
    IohcIdentityView* v = malloc(sizeof(IohcIdentityView));
    v->view = view_alloc();
    v->on_backup = NULL;
    v->on_backup_ctx = NULL;
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(IohcIdentityModel));
    view_set_draw_callback(v->view, draw_callback);
    view_set_input_callback(v->view, input_callback);
    view_set_context(v->view, v);
    return v;
}

void iohc_identity_view_free(IohcIdentityView* v) {
    view_free(v->view);
    free(v);
}

View* iohc_identity_view_get_view(IohcIdentityView* v) {
    return v->view;
}

void iohc_identity_view_set_on_backup(IohcIdentityView* v, IohcIdentityOnBackup cb, void* ctx) {
    v->on_backup = cb;
    v->on_backup_ctx = ctx;
}

void iohc_identity_view_set_identity(IohcIdentityView* v, const IohcIdentity* id) {
    with_view_model(
        v->view, IohcIdentityModel* m,
        {
            m->id = *id;
            m->has_id = true;
        },
        true);
}

void iohc_identity_view_show_toast(IohcIdentityView* v, const char* line1, const char* line2) {
    with_view_model(
        v->view, IohcIdentityModel* m,
        {
            strncpy(m->toast_line1, line1, sizeof(m->toast_line1) - 1);
            m->toast_line1[sizeof(m->toast_line1) - 1] = '\0';
            strncpy(m->toast_line2, line2, sizeof(m->toast_line2) - 1);
            m->toast_line2[sizeof(m->toast_line2) - 1] = '\0';
            m->toast_until_tick = furi_get_tick() + 2500;  // ~2.5s
        },
        true);
}
