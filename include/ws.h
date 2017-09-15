/* ws.h
 *
 * This is the main package file. Include this file in other projects.
 * Only modify inside the header-end and body-end sections.
 */

#ifndef CORTO_WS_H
#define CORTO_WS_H

#include <corto/corto.h>
#include <corto/corto.h>
#include <corto/ws/_project.h>
#include <corto/c/c.h>
#include <corto/httpserver/httpserver.h>

/* $header() */
/* Enter additional code here. */
/* $end */

#include <corto/ws/_type.h>
#include <corto/ws/_interface.h>
#include <corto/ws/_load.h>
#include <corto/ws/c/_api.h>

/* $body() */
corto_string ws_serializer_serialize(corto_value *v, bool summary);
corto_string ws_serializer_escape(char *str, size_t *length_out);
/* $end */

#endif

