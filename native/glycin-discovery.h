#pragma once
#include <gio/gio.h>
G_BEGIN_DECLS
/**
 * image_discover_glycin_loaders:
 *
 * Returns supported MIME types discovered from installed Glycin configurations.
 *
 * Returns: A NULL-terminated owned MIME list; release it with g_strfreev().
 */
char **image_discover_glycin_loaders(void);
/**
 * image_mime_matches:
 * @first: First MIME type being compared.
 * @second: Second MIME type being compared.
 *
 * Compares MIME types using the compatibility rules used for Glycin selection.
 *
 * Returns: TRUE if the MIME identifiers are compatible; FALSE otherwise.
 */
gboolean image_mime_matches(const char *first, const char *second);
G_END_DECLS
