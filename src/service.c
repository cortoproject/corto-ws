/* This is a managed file. Do not delete this comment. */

#include <corto.ws>

#define corto_lookupAssert(p, i, t) \
    corto(CORTO_LOOKUP|CORTO_ASSERT_SUCCESS|CORTO_FORCE_TYPE, {.parent = p, .id = i, .type = t})

static
void ws_service_onConnect(
    ws_service this,
    httpserver_HTTP_Connection c,
    ws_connect *clientMsg)
{
    corto_tableinstance sessions = corto_lookupAssert(this, "Session", corto_tableinstance_o);
    ws_service_Session session = NULL;
    corto_object msg = NULL;

    if (clientMsg->version && strcmp(clientMsg->version, "1.0")) {
        msg = ws_failed__create(NULL, NULL, "1.0");
        ut_warning("connect: wrong version '%s'", clientMsg->version);
    } else {
        if (!clientMsg->session || !(session = corto_lookup(sessions, clientMsg->session))) {
            char *sessionId = httpserver_random(17);
            session = ws_service_Session__create(sessions, sessionId);
            corto_set_ref(&session->conn, c);
            corto_set_ref(&c->ctx, session);
            ut_trace("connect: established session '%s'", sessionId);
            corto_dealloc(sessionId);
        } else {
            corto_set_ref(&session->conn, c);
            corto_set_ref(&c->ctx, session);
            ut_trace("connect: reestablished session '%s'", clientMsg->session);
            corto_release(session);
        }

        msg = ws_connected__create(NULL, NULL, corto_idof(session));
    }

    ws_service_Session_send(session, msg);
    corto_delete(msg);
}

static
void ws_service_onSub(
    ws_service this,
    httpserver_HTTP_Connection c,
    ws_sub *clientMsg)
{
    ws_service_Session session = ws_service_Session(c->ctx);
    corto_tableinstance subscriptions = corto_lookupAssert(session, "Subscription", corto_tableinstance_o);
    corto_object msg = NULL;

    /* If there is an existing subscription for the specified id, delete it. */
    ws_service_Session_Subscription sub = corto_lookup(subscriptions, clientMsg->id);
    if (sub) {
        corto_delete(sub);
        corto_release(sub);
    }

    /* Create new subscription */
    sub = corto_declare(subscriptions, clientMsg->id, ws_service_Session_Subscription_o);
    if (!sub) {
        msg = ws_subfail__create(NULL, NULL, corto_idof(sub), ut_lasterr());
        ut_error("creation of subscriber failed");
    } else {
        /* Query parameters */
        corto_set_str(&sub->super.query.from, clientMsg->parent);
        corto_set_str(&sub->super.query.select, clientMsg->expr);
        corto_set_str(&sub->super.query.type, clientMsg->type);
        sub->super.query.yield_unknown = clientMsg->yield_unknown;
        /*sub->offset = clientMsg->offset;
        sub->limit = clientMsg->limit;*/
        /* Set dispatcher & instance to session and server */
        corto_set_ref(&corto_observer(sub)->instance, session);
        corto_set_ref(&corto_observer(sub)->dispatcher, this);
        /* Enable subscriber so subok is sent before alignment data */
        corto_observer(sub)->enabled = FALSE;
        /* Set if subscription requests summary data */
        sub->summary = clientMsg->summary;
        if (corto_define(sub)) {
            ut_raise();
            msg = ws_subfail__create(NULL, NULL, corto_idof(sub), ut_lasterr());
            ut_error("failed to create subscriber");
            corto_delete(sub);
            sub = NULL;
        } else {
            msg = ws_subok__create(NULL, NULL, corto_idof(sub));
            ut_trace("sub: subscriber '%s' listening to '%s', '%s'",
                clientMsg->id, clientMsg->parent, clientMsg->expr);
        }

    }

    ws_service_Session_send(session, msg);
    corto_delete(msg);
    if (sub) {
        /* Enable subscriber, aligns data */
        if (corto_subscriber_subscribe(sub, session)) {
            ut_error("ws: failed to enable subscriber: %s", ut_lasterr());
        }
    }
}

static
void ws_service_onUnsub(
    ws_service this,
    httpserver_HTTP_Connection c,
    ws_unsub *clientMsg)
{
    ws_service_Session session = ws_service_Session(c->ctx);
    corto_tableinstance subscriptions = corto_lookupAssert(session, "Subscription", corto_tableinstance_o);

    /* If there is an existing subscription for the specified id, delete it. */
    corto_subscriber sub = corto_lookup(subscriptions, clientMsg->id);
    if (sub) {
        ws_service_purge(this, sub);
        corto_delete(sub);
        corto_release(sub);
    }

    ut_trace("unsub: deleted subscriber '%s'", clientMsg->id);
}

static
void ws_service_onUpdate(
    ws_service this,
    httpserver_HTTP_Connection c,
    ws_update *updateMsg)
{
    if (corto_publish(
        CORTO_UPDATE,
        NULL,
        updateMsg->id,
        NULL,
        "text/json",
        updateMsg->v ? *updateMsg->v : NULL
    )) {
        ut_error("update: failed to update '%s'", updateMsg->id);
    }

    return;
}

static
void ws_service_on_delete(
    ws_service this,
    httpserver_HTTP_Connection c,
    ws_delete *deleteMsg)
{
    if (corto_publish(
        CORTO_DELETE,
        NULL,
        deleteMsg->id,
        NULL,
        NULL,
        0
    )) {
        ut_error("delete: failed to delete '%s'", deleteMsg->id);
    }

    return;
}

void ws_service_flush(
    ws_service this,
    corto_subscriber sub)
{
    corto_subscriber_event *e;
    ut_ll subs = ut_ll_new(), to_remove = ut_ll_new();

    /* Collect events */
    corto_lock(this);
    ut_iter it = ut_rb_iter(this->events);
    while (ut_iter_hasNext(&it)) {
        e = ut_iter_next(&it);

        /* Only take events for specified subscriber */
        if (!sub || (sub == e->subscriber)) {
            ut_ll_append(to_remove, e);
        }
    }

    while ((e = ut_ll_takeFirst(to_remove))) {
        ut_rb_remove(this->events, e);

        /* It is possible that the session has already been deleted */
        if (!corto_check_state(e->instance, CORTO_DELETED)) {
            safe_ws_service_Session_Subscription_addEvent(e->subscriber, (corto_event*)e);
            if (!ut_ll_hasObject(subs, e->subscriber)) {
                ut_ll_insert(subs, e->subscriber);
            }

        } else {
            ut_assert(corto_release(e) == 0, "event is leaking");
        }
    }
    ut_ll_free(to_remove);
    corto_unlock(this);

    /* Process events outside of lock */
    it = ut_ll_iter(subs);
    while (ut_iter_hasNext(&it)) {
        ws_service_Session_Subscription sub = ut_iter_next(&it);
        ws_service_Session_Subscription_processEvents(sub);
    }

    ut_ll_free(subs);
}

void ws_service_on_close(
    ws_service this,
    corto_httpserver_HTTP_Connection c)
{
    if (c->ctx) {
        ut_trace("close: disconnected session '%s'", corto_idof(c->ctx));
        corto_delete(c->ctx);
        corto_set_ref(&c->ctx, NULL);
    }
}

void ws_service_on_message(
    ws_service this,
    corto_httpserver_HTTP_Connection c,
    const char *msg)
{
    ut_log_push("ws");

    corto_object o = NULL;
    if (corto_deserialize(&o, "text/json", msg)) {
        ut_throw("malformed message: %s", msg);
        goto error;
    }

    if (corto_typeof(corto_typeof(o)) != corto_type(corto_struct_o)) goto error_type;
    corto_struct msgType = corto_struct(corto_typeof(o));
    if (msgType == ws_connect_o) ws_service_onConnect(this, c, ws_connect(o));
    else if (msgType == ws_sub_o) ws_service_onSub(this, c, ws_sub(o));
    else if (msgType == ws_unsub_o) ws_service_onUnsub(this, c, ws_unsub(o));
    else if (msgType == ws_update_o) ws_service_onUpdate(this, c, ws_update(o));
    else if (msgType == ws_delete_o) ws_service_on_delete(this, c, ws_delete(o));
    else goto error_type;
    corto_delete(o);
    ut_log_pop();
    return;
error_type:
    ut_error("received invalid message type: '%s'", corto_fullpath(NULL, corto_typeof(o)));
error:
    ut_log_pop();
    return;
}

void ws_service_on_poll(
    ws_service this)
{
    ws_service_flush(this, NULL);
}

#define WS_QUEUE_THRESHOLD 10000
#define WS_QUEUE_THRESHOLD_SLEEP 10000000

void ws_service_post(
    ws_service this,
    corto_event *e)
{
    corto_uint32 size = 0;

    corto_lock(this);

    /* Ignore events from destructed sessions or subscribers */
    corto_object
        observer = ((corto_subscriber_event*)e)->subscriber,
        instance = ((corto_subscriber_event*)e)->instance;

    if (corto_check_state(observer, CORTO_DELETED) ||
        (instance && corto_check_state(instance, CORTO_DELETED)))
    {
        corto_release(e);
        corto_unlock(this);
        return;
    }

    void *key = e;
    void *data = ut_rb_findOrSetPtr(this->events, &key);
    if (*(void**)data) {
        corto_release(*(void**)data);
        *(void**)key = e;
    }
    *(void**)data = e;

    size = ut_rb_count(this->events);
    corto_unlock(this);

    /* If queue is getting big, slow down publisher */
    if (size > WS_QUEUE_THRESHOLD) {
        ut_sleep(0, WS_QUEUE_THRESHOLD_SLEEP);
    }
}

void ws_service_purge(
    ws_service this,
    corto_subscriber sub)
{
    ut_ll to_remove = ut_ll_new();

    /* Purge events for specified subscriber */
    corto_lock(this);
    ut_iter it = ut_rb_iter(this->events);
    while (ut_iter_hasNext(&it)) {
        corto_subscriber_event *e = ut_iter_next(&it);
        if (e->subscriber == sub) {
            ut_ll_append(to_remove, e);
        }
    }

    it = ut_ll_iter(to_remove);
    while (ut_iter_hasNext(&it)) {
        corto_subscriber_event *e = ut_iter_next(&it);
        ut_rb_remove(this->events, e);
        corto_release(e);
    }
    corto_unlock(this);

    ut_ll_free(to_remove);
}

static
int ws_service_findEvent(
    void *ctx,
    const void *o1,
    const void *o2)
{
    const corto_subscriber_event *e1 = o1, *e2 = o2;
    int result = 0;

    if (e1->subscriber != e2->subscriber) {
        result = e1->subscriber > e2->subscriber ? 1 : -1;
    } else if (!(result = strcmp(e1->data.id, e2->data.id))) {
        result = strcmp(e1->data.parent, e2->data.parent);
    }

    return result;
}

int16_t ws_service_init(
    ws_service this)
{
    /* Initialize events tree with custom compare function */
    this->events = ut_rb_new(ws_service_findEvent, NULL);
    return 0;
}
