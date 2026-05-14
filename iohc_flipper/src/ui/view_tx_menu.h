#pragma once

#include <gui/modules/submenu.h>

typedef enum {
    IohcTxMenuUp = 1,
    IohcTxMenuDown = 2,
    IohcTxMenuStop = 3,
    IohcTxMenuPair = 4,
} IohcTxMenuChoice;

typedef void (*IohcTxMenuCallback)(IohcTxMenuChoice choice, void* ctx);

typedef struct IohcTxMenu IohcTxMenu;

IohcTxMenu* iohc_tx_menu_alloc(IohcTxMenuCallback cb, void* ctx);
void iohc_tx_menu_free(IohcTxMenu* m);
View* iohc_tx_menu_get_view(IohcTxMenu* m);
