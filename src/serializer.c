
#include <corto/ws/ws.h>

#define WS_MAX_SUMMARY_STRING (40)

typedef struct ws_serializer_t {
    int count;
    int valueCount;
    corto_buffer *buff;
    bool summary;
} ws_serializer_t;

corto_string ws_serializer_escape(char *str, size_t *length_out) {
    int length = stresc(NULL, 0, str);
    corto_string result = corto_alloc(length + 1);
    stresc(result, length, str);
    result[length] = '\0';

    if (length_out) {
        *length_out = length;
    }

    return result;
}

static corto_string ws_serializer_truncate(corto_string str) {
    str[WS_MAX_SUMMARY_STRING] = '\0';
    str[WS_MAX_SUMMARY_STRING - 1] = '.';
    str[WS_MAX_SUMMARY_STRING - 2] = '.';
    str[WS_MAX_SUMMARY_STRING - 3] = '.';
    return str;
}

static corto_int16 ws_serializer_primitive(
    corto_walk_opt* s, 
    corto_value *info, 
    void *userData) 
{
    corto_primitive t = corto_primitive(corto_value_typeof(info));
    void *ptr = corto_value_ptrof(info);
    ws_serializer_t *data = userData;
    corto_string str = NULL, prev = NULL;

    /* Binary numbers translate to hex (0x0) numbers which JSON doesn't understand.
     * translate to a regular unsigned int */
    if (t->kind == CORTO_BINARY) {
        switch(t->width) {
        case CORTO_WIDTH_8: t = corto_primitive(corto_uint8_o); break;
        case CORTO_WIDTH_16: t = corto_primitive(corto_uint16_o); break;
        case CORTO_WIDTH_32: t = corto_primitive(corto_uint32_o); break;
        case CORTO_WIDTH_64: t = corto_primitive(corto_uint64_o); break;
        case CORTO_WIDTH_WORD: t = corto_primitive(corto_uint64_o); break; /* TODO: add uintptr */
        }
    }

    if (data->count) {
        corto_buffer_appendstr(data->buff, ",");
    }

    if (corto_ptr_cast(t, ptr, corto_primitive(corto_string_o), &str)) {
        goto error;
    }

    switch(t->kind) {
    case CORTO_BOOLEAN:
    case CORTO_UINTEGER:
    case CORTO_INTEGER:
    case CORTO_FLOAT:
        if (!strcmp(str, "nan")) {
            corto_ptr_setstr(&str, "null");
        }
        corto_buffer_append(data->buff, "%s", str);
        break;
    case CORTO_TEXT: {
        size_t length = 0;
        if (!*(corto_string*)ptr) {
            corto_buffer_appendstr(data->buff, "null");
            str = NULL;
            break;
        }
        prev = str;
        str = ws_serializer_escape(str, &length);
        if (data->summary && length > WS_MAX_SUMMARY_STRING) {
            str = ws_serializer_truncate(str);
        }
        corto_dealloc(prev);
    }
    case CORTO_BITMASK:
    case CORTO_ENUM:
    case CORTO_CHARACTER:
        corto_buffer_append(data->buff, "\"%s\"", str);
        break;
    default: 
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
    corto_walk_opt* s, 
    corto_value *info, 
    void *userData) 
{
    ws_serializer_t *data = userData;
    corto_object o = *(corto_object*)corto_value_ptrof(info);
    corto_id id;
    corto_fullpath(id, o);
    char *str = ws_serializer_escape(id, NULL);
    if (data->count) {
        corto_buffer_appendstr(data->buff, ",");
    }
    corto_buffer_append(data->buff, "\"%s\"", str);
    corto_dealloc(str);
    data->count ++;
    data->valueCount ++;
    
    return 0;
}

static corto_int16 ws_serializer_object(
    corto_walk_opt* s, 
    corto_value *info, 
    void *userData) 
{
    ws_serializer_t *data = userData;
    ws_serializer_t privateData = {
        .count = 0, 
        .valueCount = 0, 
        .buff = data->buff, 
        .summary = data->summary
    };
    corto_type t = corto_value_typeof(info);

    if (data->count) corto_buffer_appendstr(data->buff, ",");
    corto_buffer_appendstr(data->buff, "[");

    if (t->kind == CORTO_COMPOSITE) {
        if (corto_walk_members(s, info, &privateData)) {
            goto error;
        }
    } else {
        if (!data->summary) {
            if (corto_walk_elements(s, info, &privateData)) {
                goto error;
            }
        } else {
            unsigned int count = corto_ptr_count(corto_value_ptrof(info), t);
            corto_buffer_append(data->buff, "%u", count);
            if (count) {
                privateData.valueCount = 1;
            }
        }
    }

    data->count ++;
    data->valueCount += privateData.valueCount;
    corto_buffer_appendstr(data->buff, "]");

    return 0;
error:
    return -1;
}

static corto_walk_opt ws_serializer(void) {
    corto_walk_opt result;

    corto_walk_init(&result);
    result.access = CORTO_PRIVATE;
    result.accessKind = CORTO_NOT;
    result.aliasAction = CORTO_WALK_ALIAS_IGNORE;
    result.optionalAction = CORTO_WALK_OPTIONAL_ALWAYS;
    result.program[CORTO_PRIMITIVE] = ws_serializer_primitive;
    result.program[CORTO_COMPOSITE] = ws_serializer_object;
    result.program[CORTO_COLLECTION] = ws_serializer_object;
    result.reference = ws_serializer_reference;
    result.metaprogram[CORTO_BASE] = corto_walk_members;

    return result;
}

corto_string ws_serializer_serialize(corto_value *v, bool summary) {
    corto_string result = NULL;
    corto_buffer buff = CORTO_BUFFER_INIT;
    ws_serializer_t walkData = {0, 0, &buff, summary};
    corto_walk_opt s = ws_serializer();

    if (corto_walk_value(&s, v, &walkData)) {
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
