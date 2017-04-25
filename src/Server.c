/* $CORTO_GENERATED
 *
 * Server.c
 *
 * Only code written between the begin and end tags will be preserved
 * when the file is regenerated.
 */

#include <corto/ws/ws.h>

/* $header() */
static void ws_Server_onConnect(ws_Server this, server_HTTP_Connection c, ws_connect *clientMsg) 
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
            corto_trace("ws: connect: established session '%s'", sessionId);
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

static void ws_Server_onSub(ws_Server this, server_HTTP_Connection c, ws_sub *clientMsg) 
{
    ws_Server_Session session = ws_Server_Session(c->udata);
    corto_tablescope subscriptions = corto_lookupAssert(session, "Subscription", corto_tablescope_o);
    corto_object msg = NULL;

    /* If there is an existing subscription for the specified id, delete it. */
    corto_subscriber sub = corto_lookup(subscriptions, clientMsg->id);
    if (sub) {
        corto_delete(sub);
        corto_release(sub);
    }

    /* Create new subscription */
    sub = corto_declareChild(subscriptions, clientMsg->id, ws_Server_Session_Subscription_o);
    if (!sub) {
        msg = ws_subfailCreate(corto_idof(sub), corto_lasterr());
        corto_error("ws: creation of subscriber failed: %s", corto_lasterr());
    } else {
    
        /* Query parameters */
        corto_setstr(&sub->parent, clientMsg->parent);
        corto_setstr(&sub->expr, clientMsg->expr);
        corto_setstr(&corto_observer(sub)->type, clientMsg->type);
        
        /*sub->offset = clientMsg->offset;
        sub->limit = clientMsg->limit;*/

        /* Set dispatcher & instance to session and server */
        corto_setref(&corto_observer(sub)->instance, session);
        corto_setref(&corto_observer(sub)->dispatcher, this);

        /* Enable subscriber */
        corto_observer(sub)->enabled = TRUE;

        if (corto_define(sub)) {
            msg = ws_subfailCreate(corto_idof(sub), corto_lasterr());
            corto_error("ws: failed to create subscriber: %s", corto_lasterr());
            corto_delete(sub);
            sub = NULL;
        } else {
            msg = ws_subokCreate(corto_idof(sub));
            corto_trace("ws: sub: subscriber '%s' listening to '%s', '%s'", 
                clientMsg->id, clientMsg->parent, clientMsg->expr);
        }
    }

    ws_Server_Session_send(session, msg);
    corto_delete(msg);

    /* Flush aligned data for quick response time */
    if (sub) {
        ws_Server_flush(this, sub);
    }
}

static void ws_Server_onUnsub(ws_Server this, server_HTTP_Connection c, ws_unsub *clientMsg) 
{    
    ws_Server_Session session = ws_Server_Session(c->udata);
    corto_tablescope subscriptions = corto_lookupAssert(session, "Subscription", corto_tablescope_o);

    /* If there is an existing subscription for the specified id, delete it. */
    corto_subscriber sub = corto_lookup(subscriptions, clientMsg->id);
    if (sub) {
        ws_Server_purge(this, sub);
        corto_delete(sub);
        corto_release(sub);
    }

    corto_trace("ws: unsub: deleted subscriber '%s'", clientMsg->id);
}

/* $end */

corto_void _ws_Server_flush(
    ws_Server this,
    corto_subscriber sub)
{
/* $begin(corto/ws/Server/flush) */
    corto_observableEvent e;
    corto_ll subs = corto_llNew();

    /* Collect events */
    corto_lock(this);
    corto_iter it = corto_llIter(this->events);
    while (corto_iterHasNext(&it)) {
        e = corto_iterNext(&it);

        /* Only take events for specified subscriber */
        if (!sub || (sub == (corto_subscriber)e->observer)) {
            corto_llIterRemove(&it);

            /* It is possible that the session has already been deleted */
            if (!corto_checkState(e->me, CORTO_DESTRUCTED)) {
                ws_Server_Session_Subscription_addEvent(e->observer, e);
                if (!corto_llHasObject(subs, e->observer)) {
                    corto_llInsert(subs, e->observer);
                }
            } else {
                corto_assert(corto_release(e) == 0, "event is leaking");
            }
        }
    }
    corto_unlock(this);

    /* Process events outside of lock */
    it = corto_llIter(subs);
    while (corto_iterHasNext(&it)) {
        ws_Server_Session_Subscription sub = corto_iterNext(&it);
        ws_Server_Session_Subscription_processEvents(sub);
    }

    corto_llFree(subs);

/* $end */
}

corto_void _ws_Server_onClose(
    ws_Server this,
    server_HTTP_Connection c)
{
/* $begin(corto/ws/Server/onClose) */
    if (c->udata) {
        corto_trace("ws: close: disconnected session '%s'", corto_idof(c->udata));
        corto_delete(c->udata);
        corto_setref(&c->udata, NULL);
    }    

/* $end */
}

corto_void _ws_Server_onMessage(
    ws_Server this,
    server_HTTP_Connection c,
    corto_string msg)
{
/* $begin(corto/ws/Server/onMessage) */
    corto_object o = corto_createFromContent("text/json", msg);
    if (!o) {
        corto_error("ws: %s (malformed message)", corto_lasterr());
        return;
    }

    if (corto_typeof(corto_typeof(o)) != corto_type(corto_struct_o)) goto error_type;

    corto_struct msgType = corto_struct(corto_typeof(o));

    if (msgType == ws_connect_o) ws_Server_onConnect(this, c, ws_connect(o));
    else if (msgType == ws_sub_o) ws_Server_onSub(this, c, ws_sub(o));
    else if (msgType == ws_unsub_o) ws_Server_onUnsub(this, c, ws_unsub(o));
    else goto error_type;
    corto_delete(o);

    return;
error_type: 
    corto_error("ws: received invalid message type: '%s'", corto_fullpath(NULL, corto_typeof(o)));
/* $end */
}

corto_void _ws_Server_onPoll(
    ws_Server this)
{
/* $begin(corto/ws/Server/onPoll) */

    ws_Server_flush(this, NULL);

/* $end */
}

/* $header(corto/ws/Server/post) */
#define WS_QUEUE_THRESHOLD 10000
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

    corto_lock(this);

    /* Ignore events from destructed sessions or subscribers */
    corto_object 
        observer = corto_observableEvent(e)->observer,
        instance = corto_observableEvent(e)->me;

    if (corto_checkState(observer, CORTO_DESTRUCTED) || 
        (instance && corto_checkState(instance, CORTO_DESTRUCTED))) 
    {
        corto_release(e);
        corto_unlock(this);
        return;
    }

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

corto_void _ws_Server_purge(
    ws_Server this,
    corto_subscriber sub)
{
/* $begin(corto/ws/Server/purge) */

    /* Purge events for specified subscriber */
    corto_lock(this);
    corto_iter it = corto_llIter(this->events);
    while (corto_iterHasNext(&it)) {
        corto_observableEvent e = corto_iterNext(&it);
        if (e->observer == corto_function(sub)) {
            corto_release(e);
            corto_llIterRemove(&it);
        }
    }
    corto_unlock(this);

/* $end */
}
