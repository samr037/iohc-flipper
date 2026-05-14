#pragma once

#include <gui/modules/submenu.h>

typedef enum {
    IohcDevActionUp = 1,
    IohcDevActionDown = 2,
    IohcDevActionStop = 3,
    IohcDevActionPair = 4,
    IohcDevActionDelete = 5,
} IohcDevAction;

typedef void (*IohcDevActionCallback)(IohcDevAction action, void* ctx);

typedef struct IohcDeviceActions IohcDeviceActions;

IohcDeviceActions* iohc_device_actions_alloc(IohcDevActionCallback cb, void* ctx);
void iohc_device_actions_free(IohcDeviceActions* a);
View* iohc_device_actions_get_view(IohcDeviceActions* a);

void iohc_device_actions_set_label(IohcDeviceActions* a, const char* header);
