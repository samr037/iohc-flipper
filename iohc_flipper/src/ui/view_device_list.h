#pragma once

#include <gui/modules/submenu.h>
#include "../state/device_book.h"

typedef void (*IohcDeviceListCallback)(uint8_t index, void* ctx);

typedef struct IohcDeviceList IohcDeviceList;

IohcDeviceList* iohc_device_list_alloc(void);
void iohc_device_list_free(IohcDeviceList* l);
View* iohc_device_list_get_view(IohcDeviceList* l);

void iohc_device_list_set_on_select(IohcDeviceList* l, IohcDeviceListCallback cb, void* ctx);

// Refresh from the device book.
void iohc_device_list_refresh(IohcDeviceList* l, const IohcDeviceBook* book);
