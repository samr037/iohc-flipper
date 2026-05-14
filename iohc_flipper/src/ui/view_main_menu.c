#include "view_main_menu.h"
#include <furi.h>

struct IohcMainMenu {
    Submenu* submenu;
    IohcMainMenuCallback cb;
    void* ctx;
};

static void on_select(void* ctx, uint32_t index) {
    IohcMainMenu* m = (IohcMainMenu*)ctx;
    if(m->cb) m->cb((IohcMainMenuChoice)index, m->ctx);
}

IohcMainMenu* iohc_main_menu_alloc(IohcMainMenuCallback cb, void* ctx) {
    IohcMainMenu* m = malloc(sizeof(IohcMainMenu));
    m->submenu = submenu_alloc();
    m->cb = cb;
    m->ctx = ctx;
    submenu_set_header(m->submenu, "iohc remote");
    submenu_add_item(m->submenu, "Sniff & capture",  IohcMainMenuCapture,  on_select, m);
    submenu_add_item(m->submenu, "Saved shutters",   IohcMainMenuDevices,  on_select, m);
    submenu_add_item(m->submenu, "Live sniffer",     IohcMainMenuSniffer,  on_select, m);
    submenu_add_item(m->submenu, "Identity",         IohcMainMenuIdentity, on_select, m);
    return m;
}

void iohc_main_menu_free(IohcMainMenu* m) {
    submenu_free(m->submenu);
    free(m);
}

View* iohc_main_menu_get_view(IohcMainMenu* m) {
    return submenu_get_view(m->submenu);
}
