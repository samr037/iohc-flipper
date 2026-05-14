#pragma once

#include <gui/view.h>
#include "../state/identity.h"

typedef struct IohcIdentityView IohcIdentityView;

typedef void (*IohcIdentityOnBackup)(void* ctx);

IohcIdentityView* iohc_identity_view_alloc(void);
void iohc_identity_view_free(IohcIdentityView* v);
View* iohc_identity_view_get_view(IohcIdentityView* v);

void iohc_identity_view_set_identity(IohcIdentityView* v, const IohcIdentity* id);
void iohc_identity_view_set_on_backup(IohcIdentityView* v, IohcIdentityOnBackup cb, void* ctx);

// Show a transient "backup saved at <path>" message.
void iohc_identity_view_show_toast(IohcIdentityView* v, const char* line1, const char* line2);
