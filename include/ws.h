/* ws.h
 * This is the main package file. Include this file in other projects.
 * Only modify inside the header-end and body-end sections.
 */

#ifndef CORTO_WS_H
#define CORTO_WS_H

#include "bake_config.h"

#define CORTO_WS_ETC ut_locate("corto.ws", NULL, UT_LOCATE_ETC)

/* $header() */
/* Enter additional code here. */
/* $end */

#include "_type.h"
#include "_interface.h"
#include "_load.h"
#include "_binding.h"

#include <corto.ws.c>

/* $body() */
corto_string ws_serializer_serialize(corto_value *v, bool summary);
corto_string ws_serializer_escape(char *str, size_t *length_out);
/* $end */

#endif

