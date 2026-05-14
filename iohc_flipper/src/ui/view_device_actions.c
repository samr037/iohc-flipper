#include "view_device_actions.h"
#include <furi.h>

struct IohcDeviceActions {
    Submenu* submenu;
    IohcDevActionCallback cb;
    void* ctx;
};

static void on_select(void* ctx, uint32_t index) {
    IohcDeviceActions* a = (IohcDeviceActions*)ctx;
    if(a->cb) a->cb((IohcDevAction)index, a->ctx);
}

IohcDeviceActions* iohc_device_actions_alloc(IohcDevActionCallback cb, void* ctx) {
    IohcDeviceActions* a = malloc(sizeof(IohcDeviceActions));
    a->submenu = submenu_alloc();
    a->cb = cb;
    a->ctx = ctx;
    submenu_add_item(a->submenu, "UP",   IohcDevActionUp,     on_select, a);
    submenu_add_item(a->submenu, "DOWN", IohcDevActionDown,   on_select, a);
    submenu_add_item(a->submenu, "STOP", IohcDevActionStop,   on_select, a);
    submenu_add_item(a->submenu, "Pair Flipper here", IohcDevActionPair, on_select, a);
    submenu_add_item(a->submenu, "Delete", IohcDevActionDelete, on_select, a);
    return a;
}

void iohc_device_actions_free(IohcDeviceActions* a) {
    submenu_free(a->submenu);
    free(a);
}

View* iohc_device_actions_get_view(IohcDeviceActions* a) {
    return submenu_get_view(a->submenu);
}

void iohc_device_actions_set_label(IohcDeviceActions* a, const char* header) {
    submenu_set_header(a->submenu, header);
}
