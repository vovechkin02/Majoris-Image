#pragma once

#include <gio/gio.h>

void worker_output_fault_arm(GCancellable *cancel);
gboolean worker_output_fault_finish(void);
