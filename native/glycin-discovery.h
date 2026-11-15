#pragma once
#include <gio/gio.h>
G_BEGIN_DECLS
char **majoris_discover_glycin_loaders(void);
gboolean majoris_mime_matches(const char *first, const char *second);
G_END_DECLS
