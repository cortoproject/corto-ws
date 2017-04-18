
#include <corto/ws/ws.h>

typedef struct ws_serializer_t {
    int count;
    int valueCount;
    corto_buffer *buff;
} ws_serializer_t;

static corto_string ws_serializer_escape(corto_string str) {
    int length = stresc(NULL, 0, str);
    corto_string result = corto_alloc(length + 1);
    stresc(result, length, str);
    result[length] = '\0';
    return result;
}

static corto_int16 ws_serializer_primitive(
    corto_serializer s, 
    corto_value *info, 
    void *userData) 
{
    corto_primitive t = corto_primitive(corto_value_getType(info));
    void *ptr = corto_value_getPtr(info);
    ws_serializer_t *data = userData;
    corto_string str = NULL, prev = NULL;

    if (data->count) {
        corto_buffer_appendstr(data->buff, ",");
    }

    if (corto_convert(t, ptr, corto_primitive(corto_string_o), &str)) {
        goto error;
    }

    switch(t->kind) {
    case CORTO_BINARY:
    case CORTO_BOOLEAN:
    case CORTO_UINTEGER:
    case CORTO_INTEGER:
    case CORTO_FLOAT:
        if (!strcmp(str, "nan")) {
            corto_setstr(&str, "null");
        }
        corto_buffer_append(data->buff, "%s", str);
        break;
    case CORTO_TEXT:
        if (!*(corto_string*)ptr) {
            corto_buffer_appendstr(data->buff, "null");
            str = NULL;
            break;
        }
        prev = str;
        str = ws_serializer_escape(str);
        corto_dealloc(prev);
    case CORTO_BITMASK:
    case CORTO_ENUM:
    case CORTO_CHARACTER:
        corto_buffer_append(data->buff, "\"%s\"", str);
        break;
    }

    if (str) corto_dealloc(str);
    data->count ++;
    data->valueCount ++;

    return 0;
error:
    return -1;
}

static corto_int16 ws_serializer_reference(
    corto_serializer s, 
    corto_value *info, 
    void *userData) 
{
    ws_serializer_t *data = userData;
    corto_object o = *(corto_object*)corto_value_getPtr(info);
    corto_id id;
    corto_fullpath(id, o);
    char *str = ws_serializer_escape(id);
    if (data->count) {
        corto_buffer_appendstr(data->buff, ",");
    }
    corto_buffer_append(data->buff, "\"%s\"", str);
    corto_dealloc(str);
    data->count ++;
    return 0;
}

static corto_int16 ws_serializer_object(
    corto_serializer s, 
    corto_value *info, 
    void *userData) 
{
    ws_serializer_t *data = userData;
    ws_serializer_t privateData = {.count = 0, .valueCount = 0, .buff = data->buff};
    corto_type t = corto_value_getType(info);

    if (data->count) corto_buffer_appendstr(data->buff, ",");
    corto_buffer_appendstr(data->buff, "[");

    if (t->kind == CORTO_COMPOSITE) {
        if (corto_serializeMembers(s, info, &privateData)) {
            goto error;
        }
    } else {
        if (corto_serializeElements(s, info, &privateData)) {
            goto error;
        }
    }

    data->count ++;
    data->valueCount += privateData.valueCount;
    corto_buffer_appendstr(data->buff, "]");

    return 0;
error:
    return -1;
}

static struct corto_serializer_s ws_serializer(void) {
    struct corto_serializer_s result;

    corto_serializerInit(&result);
    result.access = CORTO_PRIVATE;
    result.accessKind = CORTO_NOT;
    result.aliasAction = CORTO_SERIALIZER_ALIAS_IGNORE;
    result.optionalAction = CORTO_SERIALIZER_OPTIONAL_ALWAYS;
    result.program[CORTO_PRIMITIVE] = ws_serializer_primitive;
    result.program[CORTO_COMPOSITE] = ws_serializer_object;
    result.program[CORTO_COLLECTION] = ws_serializer_object;
    result.reference = ws_serializer_reference;
    result.metaprogram[CORTO_BASE] = NULL;

    return result;
}

corto_string ws_serializer_serialize(corto_object o) {
    corto_string result = NULL;
    corto_buffer buff = CORTO_BUFFER_INIT;
    ws_serializer_t walkData = {0, 0, &buff};
    struct corto_serializer_s s = ws_serializer();

    if (corto_serialize(&s, o, &walkData)) {
        goto error;
    }

    result = corto_buffer_str(&buff);

    if (!walkData.valueCount) {
        corto_dealloc(result);
        result = NULL;
    }
    
    return result;
error:
    corto_dealloc(result);
    return NULL;
}
