#include "view_tx_menu.h"
#include <furi.h>

struct IohcTxMenu {
    Submenu* submenu;
    IohcTxMenuCallback cb;
    void* ctx;
};

static void on_select(void* ctx, uint32_t index) {
    IohcTxMenu* m = (IohcTxMenu*)ctx;
    if(m->cb) m->cb((IohcTxMenuChoice)index, m->ctx);
}

IohcTxMenu* iohc_tx_menu_alloc(IohcTxMenuCallback cb, void* ctx) {
    IohcTxMenu* m = malloc(sizeof(IohcTxMenu));
    m->submenu = submenu_alloc();
    m->cb = cb;
    m->ctx = ctx;
    submenu_set_header(m->submenu, "TX as Flipper");
    submenu_add_item(m->submenu, "Pair to motor", IohcTxMenuPair, on_select, m);
    submenu_add_item(m->submenu, "UP",   IohcTxMenuUp,   on_select, m);
    submenu_add_item(m->submenu, "DOWN", IohcTxMenuDown, on_select, m);
    submenu_add_item(m->submenu, "STOP", IohcTxMenuStop, on_select, m);
    return m;
}

void iohc_tx_menu_free(IohcTxMenu* m) {
    submenu_free(m->submenu);
    free(m);
}

View* iohc_tx_menu_get_view(IohcTxMenu* m) {
    return submenu_get_view(m->submenu);
}
