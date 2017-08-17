# ws
The corto/ws project is a reference implementation of a new IoT websocket protocol (also called `corto/ws`) that is optimized for efficient usage of bandwidth while supporting dynamic, realtime and responsive web applications. The protocol is a work in progress and contributions are welcome!

## A quick introduction
Typically, websockets protocols send JSON data that looks like this:
```json
{"speed":40.2, "fuel_level": 32.5, "oil_temperature": 80.6, "rpm": 2545, "brand": "Fiat"}
```
This quickly becomes bloated, as the strings `"speed"`, `"fuel_level"`, `"oil_temperature"`, `"rpm"` and `"brand"` would be replicated for every update. We wanted to do better than that. In `corto/ws`, the same data looks like this:
```json
[40, 32, 80, 2500, "Fiat"]
```
This is obviously shorter, but also leaves out crucial information necessary to interpret the data. So in addition, the protocol also sends metadata, which in this case would look something like this:

```json
{
    "type": "Car",
    "kind": "object",
    "members": [
        {"speed": "float"},
        {"fuel_level": "float"},
        {"oil_temperature": "float"},
        {"rpm": "int"},
        {"brand": "string"}
    ]
}
```
Combined with this metadata, a client will now be able to interpret the compact JSON form, and reconstruct a full JSON object locally.

This metadata is a lot of overhead to send with every message however, so the protocol will send it only once per **session**, and assume that the client will remember it for future updates.

An additional advantage of sending metadata is that web clients become type-aware. A web app could for example use metadata to automatically generate a form that validates input based on the type description.

We built a web application that fully relies on this websocket data (`cortoproject/admin`) to visualize any data made available through the protocol. Therefore, anyone that implements this protocol will be able to use this web app (and there will be more in the future!).

## Overview of the query format
The protocol employs another strategy to reduce bandwidth usage, which is that it allows a client to precisely specify which data it is interested in. The server uses this information to only send matching data to the client. The query language has been designed for hierarchical datasets. Hierarchies partition data in a natural way that scales well, even with very large datasets.

The query format will be described in a separate document as it is a fundamental technology underpinning the corto framework, but a few examples are provided here to give an idea:

Here is an example dataset that describes some objects. The objects in the `scope` arrays are located "inside" the parent `Car` objects. Put simply: both cars contain four *children* of type `Wheel`.
```json
[{
  "id": "Car/MyCar", 
  "value": { ... },
  "scope": [
    {"id": "Wheel/FrontLeft", ... },
    {"id": "Wheel/FrontRight", ... },
    {"id": "Wheel/BackLeft", ... },
    {"id": "Wheel/BackRight", ... }
  ]
}, {
  "id": "Car/YourCar",
  "value": { ... },
  "scope": [
    {"id": "Wheel/FrontLeft", ... },
    {"id": "Wheel/FrontRight", ... },
    {"id": "Wheel/BackLeft", ... },
    {"id": "Wheel/BackRight", ... }
  ]
}]
```

### Query 1: Select object all objects from scope `Car`:
```json
{"select": "*", "from": "Car"}
```
Result:
```json
[
    {"id": "MyCar", "value": {...}},
    {"id": "YourCar", "value": {...}}
]
```
Ids in the results are relative to the `from` part of the query. If the query would have been `{"select":"Car/*"}` the result id would have been `Car/MyCar`.

### Query 2: Select all objects recursively from scope `Car`
```json
{"select": "//", "from": "Car"}
```
Result:
```json
[
    {"id": "MyCar", "value": {...}},
    {"id": "MyCar/Wheel/FrontLeft", "value": {...}},
    {"id": "MyCar/Wheel/FrontRight", "value": {...}},
    {"id": "MyCar/Wheel/BackLeft", "value": {...}},
    {"id": "MyCar/Wheel/BackRight", "value": {...}},
    {"id": "YourCar", "value": {...}},
    {"id": "YourCar/Wheel/FrontLeft", "value": {...}},
    ...
]
```

### Query 3: Select all objects recursively from scope `Car`, only show objects of type `Wheel`
```json
{"select": "//", "from": "Car", "type": "Car/Wheel"}
```
Result:
```json
[
    {"id": "MyCar/Wheel/FrontLeft", "value": {...}},
    {"id": "MyCar/Wheel/FrontRight", "value": {...}},
    {"id": "MyCar/Wheel/BackLeft", "value": {...}},
    {"id": "MyCar/Wheel/BackRight", "value": {...}},
    {"id": "YourCar/Wheel/FrontLeft", "value": {...}},
    ...
]
```

### Query 4: Select all objects recursively from scope `Car`, only show objects with identifiers `FrontLeft` or `BackLeft`
```json
{"select": "//FrontLeft|BackLeft", "from": "Car"}
```
Result:
```json
[
    {"id": "MyCar/Wheel/FrontLeft", "value": {...}},
    {"id": "YourCar/Wheel/FrontLeft", "value": {...}}
]
```
The query format will be extended in the future with features that allow specifying a subset of members, filter on member values, specify time windows and allow for map/reduce operations amongst others.

## Protocol specification
The following sections describe the different messages that are exchanged between server and client. Note that the messages use the corto JSON encoding, so they can be serialized/deserialized using the default corto JSON serializer.

### Connectivity
First a client needs to establish a connection. It does this by sending a `connect` message:
```json
{
  "type":"connect",
  "value": {
    "version":"1.0.0",
    "session":"a randomly generated id"
  }
}
``` 
The session field can be used by a client if it is reconnecting to a server and had previously obtained a session id. If the server retained information this session, it will minimize data alignment to the client. Whether or session information outlives a disconnect is implementation defined. A client should not rely on this.

If the server accepts the connection, it will send a `connected` message:
```json
{
  "type":"connected",
  "value":{
    "session":"a randomly generated string"
  }
}
```
If the client provided a session id, and the server returned a different id, the client should assume the old session is lost, and therefore discard any state belonging to the old session.

If the server cannot support the requested version, it will return a `failed` message:
```json
{
  "type":"failed",
  "value":{
    "version":"1.0.0"
  }
}
```

### Subscribing for data
After a client has established a successful connection, it can subscribe for data with a `sub` message:
```json
{
  "type":"sub",
  "value":{
    "id":"my_subscription",
    "select":"*",
    "from":"my_scope",
    "type":"my_type_filter",
    "summary":true
  }
}
```
The provided `id` is a unique id **per session** that identifies the subscription. A web application may have multiple subscriptions at the same time. Incoming data is annotated with the subscriber id.

The `select`, `from` and `type` members are explained in the Query section.

The `summary` field truncates properties that have a dynamic/unbounded length, so objects have a worst-case size. When `true`, strings are truncated to a maximum number of characters and collection properties only send the number of elements. Other properties are unaffected.

Typically an application will subscribe for a set of objects with `summary` set to `true`, and once a user of the app requires more detail, a new subscription is done with `summary` set to `false`.

Upon a successful subscription, the server will return a `subok` message:
```json
{
  "type":"subok",
  "value":{
    "id":"my_subscription"
  }
}
```
The `id` field refers to the same `id` provided in the `sub` message. A server is not required to send out `subok` messages in the same order as the client sent the `sub` messages.

If the subscription failed, the server will send a `subfail` message:
```json
{
  "type":"subfail",
  "value":{
    "id":"my_subscription",
    "error":"Why it failed"
  }
}
```

Clients can also unsubscribe for existing subscriptions with the `unsub` message:
```json
{
  "type":"unsub",
  "value":{
    "id":"my_subscription"
  }
}
```
If a client sends an `unsub` for a non-existing subscription the server will not treat this as an error as the postcondition of `unsub` is met: there shall be no subscription with the provided `id`.

### Receiving data
The meat of the protocol is in the `data` message, which contains the metadata and data. Here is a simple example that contains three objects of a composite type:
```json
{
  "type":"data",
  "value":{
    "sub":"my_subscription",
    "type":"Point",
    "kind":"composite",
    "members": {
      "x":"int",
      "y":"int"
    },
    "set":[
      {"id":"point_1","v":[10,20]},
      {"id":"point_2","v":[30,40]},
      {"id":"point_3","v":[50,60]}
    ]
  }
}
```
The `type` property in this message is the type identifier. This type identifier will be used in future messages to indicate that objects of this type are going to be received. The `kind` member specifies what kind of type is described. The `reference` member specifies whether this is a reference type, and the `members` member specifies the members for this composite type.

Each value for `kind` comes with different members. For example, the `members` property is only relevant for `composite` types. These members will be described in more detail in the "simple typesystem" corto specification.

The `set` member contains an array of object identifiers and new object values.

A subsequent message with `Point` objects does not contain the metadata:
```json
{
  "type":"data",
  "value":{
    "sub":"my_subscription",
    "type":"Point",
    "set":[
      {"id":"point_1","v":[11,21]},
      {"id":"point_2","v":[31,41]},
      {"id":"point_3","v":[51,61]}
    ]
  }
}
```

The protocol also supports nested types. Here is a message that contains a type description for a `Line` type that is composed out of two members of a `Point` type. Assume that this is the first message that the client receives:
```json
{
  "type":"data",
  "value":{
    "sub":"my_subscription",
    "data":[
      {
        "type": "Point",
        "kind": "composite",
        "members": {
          "x":"int",
          "y":"int"
        }
      },
      {
        "type": "Line",
        "kind": "composite",
        "members": {
          "start":"Point",
          "stop":"Point"
        },
        "set":[
          {"id":"line_1","v":[[10,20],[30,40]]},
          {"id":"line_2","v":[[50,60],[70,80]]},
          {"id":"line_3","v":[[90,100],[110,120]]},
        ]
      }
    ]
  }
}
```
Notice how the object values use nested `[]` to indicate the nested composite value. Also note that when there are multiple types in a single message, a `data` member is added that contains a collection of types.

A subsequent message with Line objects looks like this:
```json
{
  "type":"data",
  "value":{
    "sub":"my_subscription",
    "type": "Line",
    "set":[
      {"id":"line_1","v":[[11,21],[31,41]]},
      {"id":"line_2","v":[[51,61],[71,81]]},
      {"id":"line_3","v":[[91,101],[111,121]]},
    ]
  }
}
```
A single message may contain objects of multiple types. This goes for both messages with and messages without metadata. For brevity, here is an example without metadata:
```json
{
  "type":"data",
  "value":{
    "sub":"my_subscription",
    "data":[
      {
        "type": "Point",
        "set":[
          {"id":"point_1","v":[10,20]},
          {"id":"point_2","v":[30,40]},
          {"id":"point_3","v":[50,60]}
        ]
      },
      {
        "type": "Line",
        "set":[
          {"id":"line_1","v":[[10,20],[30,40]]},
          {"id":"line_2","v":[[50,60],[70,80]]},
          {"id":"line_3","v":[[90,100],[110,120]]},
        ]
      }
    ]
  }
}
```
There is no limitation on how many types or objects can be in a single message.

This document is a work in progress. What will be added next is a description of the JSON value encoding.
