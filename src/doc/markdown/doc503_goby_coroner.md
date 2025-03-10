# goby-zeromq: goby_coroner

It it often important to know if all the processes that are expected to be running on the robotic system are actually running and are responsive.

`goby_coroner` regularly publishes a **request** (heartbeat) that is subscribed to by all the applications that subclass from goby::middleware::SingleThreadApplication or goby::middleware::MultiThreadApplication. These applications each send a **response** which is aggregated into a **report** that can be monitored by some custom process to notify someone or perform some action (e.g., restart the dead process).

## goby_coroner

A simple launch script that has two goby applications (`goby_gps` and `goby_logger`) that is monitored by `goby_coroner` would look like:

```
#!/usr/bin/env -S goby_launch -P -p test

gobyd
goby_gps
goby_logger --log_dir /tmp
goby_coroner --expected_name goby_gps --expected_name goby_logger
```

You must explicitly specify `--expected_name` for an app to show up in the report from `goby_coroner`.

Run this and then you can monitor the publications:

```
goby zeromq subscribe 'goby::health::.*'
```

You'll see three publications:

1. Request from goby_coroner
```
1 | goby::health::request | goby.middleware.protobuf.HealthRequest | 2025-Mar-05 21:31:24.109233 | 
```

2. Response from applications:
```
1 | goby::health::response | goby.middleware.protobuf.ProcessHealth | 2025-Mar-05 21:31:25.409589 | name: "goby_gps" pid: 272383 main { name: "goby_gps" thread_id: 272383 state: HEALTH__OK child { name: "health_monitor" thread_id: 272392 uid: 0 state: HEALTH__OK } child { name: "tcp: 127.0.0.1:2947" thread_id: 272393 uid: 1 state: HEALTH__OK } }
1 | goby::health::response | goby.middleware.protobuf.ProcessHealth | 2025-Mar-05 21:31:24.109597 | name: "goby_logger" pid: 272410 main { name: "goby_logger" thread_id: 272412 state: HEALTH__OK }
```

3. And most importantly, the report aggregated by `goby_coroner`:

```
1 | goby::health::report | goby.middleware.protobuf.VehicleHealth | 2025-Mar-05 21:31:19.209131 | time: 1741210279208246 platform: "default_goby_platform" state: HEALTH__OK process { name: "goby_gps" pid: 272383 main { name: "goby_gps" thread_id: 272383 state: HEALTH__OK child { name: "health_monitor" thread_id: 272392 uid: 0 state: HEALTH__OK } child { name: "tcp: 127.0.0.1:2947" thread_id: 272393 uid: 1 state: HEALTH__OK } } } process { name: "goby_logger" pid: 272410 main { name: "goby_logger" thread_id: 272412 state: HEALTH__OK } }
```

Most of the time you would want to subscribe to the `goby::health::report` by some custom application specific goby app and do something with it. For example, see `jaiabot_health` app from the Jaiabot project: https://docs.jaia.tech/md_page75_health.html#autotoc_md370

The groups are defined in:

```
#include <goby/middleware/coroner/groups.h>
```

and the Protobuf messages are in [`goby/src/middleware/protobuf/coroner.proto`](https://github.com/GobySoft/goby3/blob/3.0/src/middleware/protobuf/coroner.proto) and can be included using:

```
#include <goby/middleware/protobuf/coroner.pb.h>
```

### Contents of the goby::health::report

The `goby.middleware.protobuf.VehicleHealth` protobuf message is a hierarchical and recursive message.

The various levels are:
- Platform/Vehicle (Entire system managed by one `goby_coroner`)
- Process/Application (Each Goby Application)
- Thread (For MultiThreadApplication - each thread in the Process)

At each level, a given component (vehicle, process or thread) can have one of three health statuses:
- HEALTH__OK: Everything working normally
- HEALTH__DEGRADED: Something has gone wrong but it isn't critical (e.g., battery low)
- HEALTH__FAILED: Something critical has gone wrong (e.g., application crashed)

The aggregate health status of the parent is the *worst* status of any of its children. So if one Process reports HEALTH__DEGRADED, the Vehicle is HEALTH__DEGRADED. If any thread reports HEALTH__FAILED, the Process reports HEALTH__FAILED.

Thus, the only way for the Vehicle to be HEALTH__OK is if all Processes (and all their Threads) report HEALTH__OK.

Looking at the example earlier:

```
# Name of the goby platform (as set in gobyd --interprocess 'platform: "..."')
platform: "default_goby_platform"
# Overall health of the platform (worst of all the processes' health)
state: HEALTH__OK
# Health of goby_gps
process { 
  # name and process ID
  name: "goby_gps" pid: 272383 
  # main thread health
  main { name: "goby_gps" thread_id: 272383 state: HEALTH__OK
    # health monitor thread (included in MultiThreadApplication)
    child { name: "health_monitor" thread_id: 272392 uid: 0 state: HEALTH__OK } 
    # TCP connection of goby_gps to gobyd
    child { name: "tcp: 127.0.0.1:2947" thread_id: 272393 uid: 1 state: HEALTH__OK } 
  } 
}
# Health of goby_logger  
process { name: "goby_logger" pid: 272410 
   main { name: "goby_logger" thread_id: 272412 state: HEALTH__OK } 
}
```

If `goby_gps` dies or stops responding, the report would look like this:

```
platform: "default_goby_platform"
state: HEALTH__FAILED 
process { name: "goby_gps" main { name: "goby_gps" state: HEALTH__FAILED error: ERROR__PROCESS_DIED error_message: "Process goby_gps has died" } } 
process { name: "goby_logger" pid: 272410 main { name: "goby_logger" thread_id: 272412 state: HEALTH__OK } }
```

`goby_coroner` automatically infers that goby_gps process died since it did not respond to a **request**.

### Extending or modifying the goby::health::response

If you are using goby::middleware::SingleThreadApplication or goby::middleware::MultiThreadApplication you can modify the default **response** (which is simply HEALTH__OK), which then gets passed through to the **report**.

To do so you override the virtual method in your subclass:

 ```
virtual void health(goby::middleware::protobuf::ThreadHealth& health);
```

This will be called each time your process gets a **request** from `goby_coroner` and the contents of `health` after this function completes is what is reported in the **response**.

The same method can be overriden for each Thread within `goby::middleware::MultiThreadApplication`, as desired.

Finally, you can extend the ThreadHealth message using Protobuf extensions to include any custom data you want to pass out in the goby_coroner **report**. For example, see the JaiaBot project, which extends ThreadHealth to add project specific warning and error enumerations: https://docs.jaia.tech/health_8proto_source.html

If you are using the extensions for a private project, simply choose any value over 1000. For projects that are public or should interoperate you can post an issue to https://github.com/GobySoft/goby3/issues requested an extension assignment.