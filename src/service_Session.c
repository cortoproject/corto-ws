/* This is a managed file. Do not delete this comment. */

#include <corto.ws>

void ws_service_Session_send(
    ws_service_Session this,
    corto_object msg)
{
    corto_string msgJson = corto_serialize(msg, "text/json");
    httpserver_HTTP_Connection_write(this->conn, msgJson);
    corto_dealloc(msgJson);
}
