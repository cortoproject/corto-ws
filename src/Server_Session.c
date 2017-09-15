/* This is a managed file. Do not delete this comment. */

#include <corto/ws/ws.h>
void ws_Server_Session_send(
    ws_Server_Session this,
    corto_object msg)
{
    corto_string msgJson = corto_object_contentof(msg, "text/json");
    httpserver_HTTP_Connection_write(this->conn, msgJson);
    corto_dealloc(msgJson);

}

