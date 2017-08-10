/* This is a managed file. Do not delete this comment. */

#include <corto/ws/ws.h>
void ws_Server_Session_Subscription_addEvent(
    ws_Server_Session_Subscription this,
    corto_event *e)
{
    corto_ll_append(this->batch, e);
}

int16_t ws_Server_Session_Subscription_construct(
    ws_Server_Session_Subscription this)
{
    corto_ptr_setstr(&corto_subscriber(this)->contentType, "binary/corto");
    return corto_subscriber_construct(this);
}


typedef struct ws_typeSerializer_t {
    ws_Server_Session session;
    ws_data *msg;
    ws_dataType *dataType;
    corto_buffer memberBuff;
    corto_int32 count;
} ws_typeSerializer_t;

ws_dataType* ws_data_addMetadata(
    ws_Server_Session session, 
    ws_data *msg, 
    corto_type t);

corto_int16 ws_typeSerializer_member(corto_walk_opt* s, corto_value *info, void *userData) {
    ws_typeSerializer_t *data = userData;
    corto_type type = corto_value_typeof(info);
    corto_member m = info->is.member.t;

    if (data->count) {
        corto_buffer_appendstr(&data->memberBuff, ",");
    } else {
        corto_buffer_appendstr(&data->memberBuff, "{"); 
    }

    corto_id typeId;
    corto_fullpath(typeId, type);
    corto_string escaped = ws_serializer_escape(typeId, NULL);
    corto_buffer_append(&data->memberBuff, "\"%s\":\"%s\"", corto_idof(m), escaped);
    corto_dealloc(escaped);

    data->count ++;

    ws_data_addMetadata(data->session, data->msg, type);

    return 0;
}

corto_int16 ws_typeSerializer_constant(corto_walk_opt* s, corto_value *info, void *userData) {
    ws_typeSerializer_t *data = userData;
    corto_type type = corto_value_typeof(info);
    corto_constant *c = info->is.constant.t;

    if (!data->dataType->constants) {
        data->dataType->constants = corto_alloc(sizeof(corto_ll));
        *data->dataType->constants = corto_ll_new();
    }
    corto_stringListAppend(*data->dataType->constants, corto_idof(c));
    ws_data_addMetadata(data->session, data->msg, type);
    return 0;
}

corto_int16 ws_typeSerializer_element(corto_walk_opt* s, corto_value *info, void *userData) {
    ws_typeSerializer_t *data = userData;
    corto_collection type = corto_collection(corto_value_typeof(info));
    data->dataType->elementType = corto_calloc(sizeof(char*));
    {
            corto_id elementTypeId;
            corto_ptr_setstr(data->dataType->elementType, corto_fullpath(elementTypeId, type->elementType));
    }
    ws_data_addMetadata(data->session, data->msg, type->elementType);
    return 0;
}

static corto_walk_opt ws_typeSerializer(void) {
    corto_walk_opt result;

    corto_walk_init(&result);
    result.access = CORTO_PRIVATE;
    result.accessKind = CORTO_NOT;
    result.aliasAction = CORTO_WALK_ALIAS_IGNORE;
    result.optionalAction = CORTO_WALK_OPTIONAL_ALWAYS;
    result.metaprogram[CORTO_MEMBER] = ws_typeSerializer_member;
    result.metaprogram[CORTO_CONSTANT] = ws_typeSerializer_constant;
    result.program[CORTO_COLLECTION] = ws_typeSerializer_element;

    return result;
}

static ws_dataType* ws_data_findDataType(ws_data *data, corto_type type) {
    if (corto_ll_size(data->data)) {
        corto_id typeId;
        corto_fullpath(typeId, type);    
        corto_iter it = corto_ll_iter(data->data);
        while (corto_iter_hasNext(&it)) {
            ws_dataType *dataType = corto_iter_next(&it);
            if (!strcmp(dataType->type, typeId)) {
                return dataType;
            }
        }
    }
    return NULL;
}

ws_dataType* ws_data_addMetadata(
    ws_Server_Session session, 
    ws_data *msg, 
    corto_type t) 
{
    bool appendType = false;
    ws_dataType *dataType = ws_data_findDataType(msg, t);
    if (!dataType) {
        dataType = corto_calloc(sizeof(ws_dataType));
        corto_ptr_setstr(&dataType->type, corto_fullpath(NULL, t));
        appendType = true;
    }

    if (!corto_ll_hasObject(session->typesAligned, t)) {
        corto_type kind = corto_typeof(t);
        corto_stringSet(dataType->kind, corto_fullpath(NULL, kind));
        corto_walk_opt s = ws_typeSerializer();
        ws_typeSerializer_t walkData = {session, msg, dataType, CORTO_BUFFER_INIT, 0};
        corto_ll_insert(session->typesAligned, t);
        corto_metawalk(&s, t, &walkData);
        if (walkData.count) {
            corto_buffer_appendstr(&walkData.memberBuff, "}");
            corto_string members = corto_buffer_str(&walkData.memberBuff);
            corto_stringSet(dataType->members, members);
            corto_dealloc(members);
        }
        if (t->reference) {
            corto_boolSet(dataType->reference, TRUE);
        }
    }

    if (appendType) {
        corto_ll_append(msg->data, dataType);
    }

    return dataType;
}


void ws_Server_Session_Subscription_processEvents(
    ws_Server_Session_Subscription this)
{
    ws_Server_Session session = corto_parentof(corto_parentof(this));

    corto_debug("ws: prepare %d events for '%s' [%p]",
        corto_ll_size(this->batch),
        corto_fullpath(NULL, this), this);

    ws_data *msg = corto_declare(ws_data_o);
    corto_ptr_setstr(&msg->sub, corto_idof(this));

    corto_subscriberEvent *e;
    while ((e = corto_ll_takeFirst(this->batch))) {
        ws_dataObject *dataObject = NULL;
        void *data = (void*)e->data.value;
        if (!data) {
            corto_warning("ws: event for '%s' does not set the value", e->data.id);
            corto_release(e);
            continue;
        }

        corto_type t = corto_resolve(NULL, e->data.type);
        ws_dataType *dataType = ws_data_addMetadata(session, msg, t);

        corto_eventMask mask = e->event;
        if (mask & (CORTO_ON_UPDATE|CORTO_ON_DEFINE)) {
            if (!dataType->set) {
                dataType->set = corto_alloc(sizeof(corto_ll));
                *dataType->set = corto_ll_new();
            }
            dataObject = ws_dataObjectListAppendAlloc(*dataType->set);
        } else if (mask & CORTO_ON_DELETE) {
            if (!dataType->del) {
                dataType->del = corto_alloc(sizeof(corto_ll));
                *dataType->del = corto_ll_new();
            }
            dataObject = ws_dataObjectListAppendAlloc(*dataType->del);   
        }

        if (dataObject) {
            corto_ptr_setstr(&dataObject->id, e->data.id);
            if (strcmp(e->data.parent, ".")) {
                corto_stringSet(dataObject->p, e->data.parent);
            }

            /* Don't serialize for DELETE */
            if (!(mask & CORTO_ON_DELETE)) {
                corto_value v = corto_value_mem((void*)e->data.value, t);
                corto_string value = ws_serializer_serialize(&v, this->summary);
                if (value) {
                    corto_stringSet(dataObject->v, NULL);
                    *(corto_string*)dataObject->v = value;
                }
            }
        }

        corto_assert(corto_release(e) == 0, "event is leaking");

        corto_release(t);
    }

    corto_define(msg);

    ws_Server_Session_send(session, msg);

    corto_delete(msg);

}

