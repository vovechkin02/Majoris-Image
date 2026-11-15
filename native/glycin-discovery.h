#pragma once
#include <gio/gio.h>
G_BEGIN_DECLS
char **image_discover_glycin_loaders(void);
gboolean image_mime_matches(const char *first, const char *second);
G_END_DECLS
