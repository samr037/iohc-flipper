#include "view_device_list.h"
#include <furi.h>
#include <stdio.h>

#define LABEL_BUF_SIZE (IOHC_DEVICE_NAME_MAX + 1 + 4)  // "[V] " prefix
#define MAX_LABELS     IOHC_DEVICE_BOOK_MAX

struct IohcDeviceList {
    Submenu* submenu;
    IohcDeviceListCallback cb;
    void* ctx;
    char labels[MAX_LABELS][LABEL_BUF_SIZE];  // submenu_add_item holds the pointer
};

static char vendor_prefix(uint8_t v) {
    if(v == 0x43) return 'S';
    if(v == 0x61) return 'V';
    return '?';
}

static void on_select(void* ctx, uint32_t index) {
    IohcDeviceList* l = (IohcDeviceList*)ctx;
    if(l->cb) l->cb((uint8_t)index, l->ctx);
}

IohcDeviceList* iohc_device_list_alloc(void) {
    IohcDeviceList* l = malloc(sizeof(IohcDeviceList));
    l->submenu = submenu_alloc();
    l->cb = NULL;
    l->ctx = NULL;
    submenu_set_header(l->submenu, "Saved shutters");
    return l;
}

void iohc_device_list_free(IohcDeviceList* l) {
    submenu_free(l->submenu);
    free(l);
}

View* iohc_device_list_get_view(IohcDeviceList* l) {
    return submenu_get_view(l->submenu);
}

void iohc_device_list_set_on_select(IohcDeviceList* l, IohcDeviceListCallback cb, void* ctx) {
    l->cb = cb;
    l->ctx = ctx;
}

void iohc_device_list_refresh(IohcDeviceList* l, const IohcDeviceBook* book) {
    submenu_reset(l->submenu);
    submenu_set_header(l->submenu, "Saved shutters");
    uint8_t n = iohc_device_book_count(book);
    if(n == 0) {
        submenu_add_item(l->submenu, "(empty — use Sniff)", 0, on_select, l);
        return;
    }
    for(uint8_t i = 0; i < n; i++) {
        const IohcDevice* d = iohc_device_book_get(book, i);
        snprintf(l->labels[i], LABEL_BUF_SIZE, "[%c] %s", vendor_prefix(d->vendor), d->name);
        submenu_add_item(l->submenu, l->labels[i], i, on_select, l);
    }
}
