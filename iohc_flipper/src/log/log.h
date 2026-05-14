#pragma once

#include "../phy/phy_rx.h"

typedef struct IohcLog IohcLog;

IohcLog* iohc_log_alloc(void);
void iohc_log_free(IohcLog* log);
void iohc_log_write(IohcLog* log, const IohcCapturedFrame* frame);
