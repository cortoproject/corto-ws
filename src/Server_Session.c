/* $CORTO_GENERATED
 *
 * Server_Session.c
 *
 * Only code written between the begin and end tags will be preserved
 * when the file is regenerated.
 */

#include <corto/ws/ws.h>

void _ws_Server_Session_send(
    ws_Server_Session this,
    corto_object msg)
{
/* $begin(corto/ws/Server/Session/send) */
    corto_string msgJson = corto_object_contentof(msg, "text/json");
    server_HTTP_Connection_write(this->conn, msgJson);
    corto_dealloc(msgJson);

/* $end */
}
