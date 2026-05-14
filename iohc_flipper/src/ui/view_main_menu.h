#pragma once

#include <gui/modules/submenu.h>

typedef enum {
    IohcMainMenuCapture = 1,
    IohcMainMenuDevices = 2,
    IohcMainMenuSniffer = 5,
    IohcMainMenuIdentity = 6,
} IohcMainMenuChoice;

typedef void (*IohcMainMenuCallback)(IohcMainMenuChoice choice, void* ctx);

typedef struct IohcMainMenu IohcMainMenu;

IohcMainMenu* iohc_main_menu_alloc(IohcMainMenuCallback cb, void* ctx);
void iohc_main_menu_free(IohcMainMenu* m);
View* iohc_main_menu_get_view(IohcMainMenu* m);
