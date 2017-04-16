/* $CORTO_GENERATED
 *
 * Server.c
 *
 * Only code written between the begin and end tags will be preserved
 * when the file is regenerated.
 */

#include <corto/ws/ws.h>

corto_void _ws_Server_onClose(
    ws_Server this,
    server_HTTP_Connection c)
{
/* $begin(corto/ws/Server/onClose) */
    if (c->udata) {
        corto_trace("ws: close: session '%s' disconnected", corto_idof(c->udata));
        corto_delete(c->udata);
        corto_setref(&c->udata, NULL);
    }    

/* $end */
}

/* $header(corto/ws/Server/onData) */
void ws_Server_onConnect(ws_Server this, server_HTTP_Connection c, ws_connect *clientMsg) 
{
    corto_tablescope sessions = corto_lookupAssert(this, "Session", corto_tablescope_o);
    ws_Server_Session session = NULL;
    corto_object msg = NULL;

    if (clientMsg->version && strcmp(clientMsg->version, "1.0")) {
        msg = ws_failedCreate("1.0");
        corto_warning("ws: connect: wrong version '%s'", clientMsg->version);
    } else {
        if (!clientMsg->session || !(session = corto_lookup(sessions, clientMsg->session))) {
            char *sessionId = server_random(17);
            session = ws_Server_SessionCreateChild(sessions, sessionId);
            corto_setref(&session->conn, c);
            corto_setref(&c->udata, session);
            corto_trace("ws: connect: session '%s' established", sessionId);
            corto_dealloc(sessionId);
        } else {
            corto_setref(&session->conn, c);
            corto_setref(&c->udata, session);
            corto_trace("ws: connect: reestablished session '%s'", clientMsg->session);
            corto_release(session);
        }
        msg = ws_connectedCreate(corto_idof(session));
    }

    ws_Server_Session_send(session, msg);
    corto_delete(msg);
}

void ws_Server_onSub(ws_Server this, server_HTTP_Connection c, ws_connect clientMsg) 
{
}

void ws_Server_onUnsub(ws_Server this, server_HTTP_Connection c, ws_connect clientMsg) 
{
}

/* $end */
corto_void _ws_Server_onData(
    ws_Server this,
    server_HTTP_Connection c,
    corto_string msg)
{
/* $begin(corto/ws/Server/onData) */
    corto_object o = corto_createFromContent("text/json", msg);
    if (!o) {
        corto_error("ws: %s (invalid message)", corto_lasterr());
        return;
    }

    corto_struct msgType = corto_struct(corto_typeof(o));

    if (msgType == ws_connect_o) ws_Server_onConnect(this, c, ws_connect(o));
    else if (msgType == ws_sub_o) {

    } else if (msgType == ws_unsub_o) {

    } else {
        corto_error("unexpected message type '%s' received from client",
            corto_fullpath(NULL, msgType));
    }

/* $end */
}

corto_void _ws_Server_onPoll(
    ws_Server this)
{
/* $begin(corto/ws/Server/onPoll) */
    corto_event e;
    corto_ll events = corto_llNew();

    /* Poll SockJs so it can send out heartbeats */
    server_SockJs_onPoll_v(this);

    /* Collect events */
    corto_lock(this);
    while ((e = corto_llTakeFirst(this->events))) {
        corto_llAppend(events, e);
    }
    corto_unlock(this);

    /* Handle events outside of lock */
    while ((e = corto_llTakeFirst(events))) {
        corto_event_handle(e);
        corto_release(e);

        /* If processing lots of events, ensure that SockJs gets a chance to
         * send out heartbeats */
        server_SockJs_onPoll_v(this);
    }

    corto_llFree(events);
/* $end */
}

/* $header(corto/ws/Server/post) */
#define WS_QUEUE_THRESHOLD 100
#define WS_QUEUE_THRESHOLD_SLEEP 10000000

static corto_subscriberEvent ws_Server_findEvent(ws_Server this, corto_subscriberEvent e) {
    corto_iter iter = corto_llIter(this->events);
    corto_subscriberEvent e2;
    while ((corto_iterHasNext(&iter))) {
        e2 = corto_iterNext(&iter);
        if (!strcmp(e2->result.id, e->result.id) &&
            !strcmp(e2->result.parent, e->result.parent) &&
            (corto_observableEvent(e2)->observer == corto_observableEvent(e)->observer))
        {
            return e2;
        }
    }
    return NULL;
}
/* $end */
corto_void _ws_Server_post(
    ws_Server this,
    corto_event e)
{
/* $begin(corto/ws/Server/post) */
    corto_uint32 size = 0;
    corto_subscriberEvent e2;

    /* Append new event to queue */
    corto_lock(this);

    /* Check if there is already another event in the queue for the same object.
     * if so, replace event with latest update. */
    if ((e2 = ws_Server_findEvent(this, corto_subscriberEvent(e)))) {
        corto_llReplace(this->events, e2, e);
        corto_assert(corto_release(e2) == 0, "event is leaking");
    } else {
        corto_llAppend(this->events, e);
    }

    size = corto_llSize(this->events);
    corto_unlock(this);

    /* If queue is getting big, slow down publisher */
    if (size > WS_QUEUE_THRESHOLD) {
        corto_sleep(0, WS_QUEUE_THRESHOLD_SLEEP);
    }

/* $end */
}
