module Goby

# Load the module and generate the functions
using CxxWrap
using ProtoBuf
using ThreadPools

export publish, subscribe

struct TaskID
    id::Int64
end

# Multi-threaded: run timers for loop, etc. on thread 1
main_task_id = TaskID(1)
timer_task_id = TaskID(2)
# Multi-threaded: start first non-main task on thread 2
next_task_id = TaskID(3)

# Single-threaded, using thread 1
# Multi-threaded: use separate thread (updated below)
cxx_task_id = TaskID(1)

is_multithreaded = false

# main task Protobuf publish
# TODO: add more schemes as additional publish functions
function publish(app, layer, group, msg::AbstractProtoBufMessage)
    layer_int::Int32 = Int32(layer)    
    if layer_int == Int32(Goby.INTERTHREAD)
        publish_interthread(main_task_id, group, msg)
        return
    end
    
    if Goby.is_multithreaded && Threads.threadid() != Goby.cxx_task_id.id
        publish_forward_interprocess(app, layer, group, msg)
        return
    end
    
    scheme = Goby.PROTOBUF
    io = IOBuffer()
    e = ProtoEncoder(io);
    encode(e, msg)
    bytes = take!(io)
    vec = StdVector{UInt8}(bytes)
    type_name = string(nameof(typeof(msg)))

    
    threadid=Threads.threadid()
    println("Cxx publish: $threadid")
    
    Goby.cxx_publish(app, layer_int, type_name, scheme, group, vec)
end

# main task (interthread)
function publish(app, layer, group, msg)
    layer_int::Int32 = Int32(layer)
    if layer_int == Int32(Goby.INTERTHREAD)
        # this would be the main task as all child tasks would use task_id::TaskID method
        publish_interthread(main_task_id, group, msg)
        return
    end

    msg_type = typeof(msg)
    throw(MethodError("publish not support for layer $layer with message type $msg_type"))
end

# child tasks (interthread)
function publish(task_id::TaskID, layer, group, msg)
    layer_int::Int32 = Int32(layer)
    if layer_int == Int32(Goby.INTERTHREAD)
        publish_interthread(task_id, group, msg)
        return
    end

    msg_type = typeof(msg)
    throw(MethodError("publish not support for layer $layer with message type $msg_type"))
end
    
interprocess_callbacks=Dict{Int, Dict{Int, Dict{String, Dict{String, Function}}}}()

function pb_name_from_callback(callback::Function)
    return string(nameof(pb_type_from_callback(callback)))
end

function pb_type_from_callback(callback::Function)
    return methods(callback)[1].sig.parameters[2]
end

function subscribe(task_id::TaskID, layer, group, callback::Function)
    layer_int::Int32 = Int32(layer)
    if layer_int == Int32(Goby.INTERTHREAD)        
        subscribe_interthread(task_id, group, callback)
        return
    end

    throw(MethodError("subscribe not support for layer $layer with task_id"))
end
                  

function subscribe(app, layer, group, callback::Function; scheme = Goby.NULL_SCHEME, type_name::String = "")
    layer_int::Int32 = Int32(layer)

    if layer_int == Int32(Goby.INTERTHREAD)        
        subscribe_interthread(main_task_id, group, callback)
        return
    end

    inferred_scheme = scheme
    inferred_type_name::String = type_name
    
    for m in methods(callback)
        sig = Base.unwrap_unionall(m.sig)     # remove type wrappers like UnionAll
        argtypes = sig.parameters
        if length(argtypes) != 2 # parameters includes type of function and arguments
            throw(ArgumentError("Function must have exactly one argument"))
        end

        # Protobuf Subscribe (based on function argument being AbstractProtoBufMessage)
        arg = argtypes[2]
        if arg <: AbstractProtoBufMessage
            inferred_scheme = Goby.PROTOBUF
            inferred_type_name = pb_name_from_callback(callback)
        end
        # TODO: add more schemes
    end

    if inferred_scheme == Goby.NULL_SCHEME
        throw(ArgumentError("Could not infer scheme from callback argument for \"$(callback)\". Ensure you have the correct argument type for your callback defined or explicitly pass the scheme and type_name to subscribe"))
    end   

    # build up nested dictionary, adding subdictionaries as needed as we go
    lvl1 = get!(interprocess_callbacks, layer_int) do
        Dict{Int, Dict{String, Dict{String, Function}}}()
    end
    lvl2 = get!(lvl1, inferred_scheme) do
        Dict{String, Dict{String, Function}}()
    end    
    lvl3 = get!(lvl2, inferred_type_name) do
        Dict{String, Function}()
    end    
    lvl3[group] = callback

    println("Subscribing to $(inferred_type_name) (Scheme: $(inferred_scheme)) on group $(group)")
    
    Goby.cxx_subscribe(app, layer_int, inferred_type_name, inferred_scheme, group, "receive", "Goby")
end

function receive(cxx_layer, cxx_type_name, cxx_scheme, cxx_group, vec::CxxRef{StdVector{UInt8}})

    layer::Int = CxxWrap.dereference_argument(cxx_layer)
    scheme::Int = CxxWrap.dereference_argument(cxx_scheme)
    type_name::String = CxxWrap.dereference_argument(cxx_type_name)
    group::String = CxxWrap.dereference_argument(cxx_group)
    dvec::StdVector{UInt8} = CxxWrap.dereference_argument(vec)
    bytes::Vector{UInt8} = reinterpret(UInt8, collect(dvec))


    if Goby.is_multithreaded && Threads.threadid() != Goby.main_task_id.id
        receive_forward_interprocess(layer, type_name, scheme, group, bytes)
        return
    end

    receive_dereferenced(layer, type_name, scheme, group, bytes)
end


function receive_dereferenced(layer, type_name, scheme, group, bytes)
    println("Received message $(type_name) (Scheme: $(scheme)) on group $(group)")
    if scheme == Goby.PROTOBUF
        callback = interprocess_callbacks[layer][scheme][type_name][group]
        io = IOBuffer(bytes)
        d::ProtoDecoder = ProtoDecoder(io)        
        msg = decode(d, pb_type_from_callback(callback))
        callback(msg)
    end
end

function read_cli_cfg()
    if length(ARGS) != 1
        println("Usage: $PROGRAM_FILE config.pb.cfg")
        exit(1)
    end

    filename = ARGS[1]
    if !ispath(filename)
        println("Error: Configuration file not found - $filename")
        exit(1)
    end

    return read(filename, String)
end

function run(goby_app)
    Goby.cxx_run(goby_app)
end

########################
# Interthread          #
# Using Tasks/Channels #
########################

# map of module to task_id
task_module_to_id=Dict{Module, TaskID}()
# map of task_id to vector of channels (used for 'check')
task_interthread_channels=Dict{TaskID, Channel}()
channel_size = 50
task_interthread_channels[main_task_id] = Channel(channel_size)

# map of group to vector of channels (used for 'publish')
group_interthread_channels=Dict{String, Vector{Channel}}()
group_interthread_channels_lock = Threads.ReentrantLock()

function publish_interthread(task_id::TaskID, group, msg)
    Threads.lock(group_interthread_channels_lock) do
        if haskey(group_interthread_channels, group)
            for channel in group_interthread_channels[group]
                put!(channel, (:interthread_receive, group, msg))
            end
        end
    end
end

function publish_forward_interprocess(app, layer, group, msg)
    put!(task_interthread_channels[Goby.cxx_task_id], (:cxx_publish, app, layer, group, msg))
end
        
function receive_forward_interprocess(layer, type_name, scheme, group, bytes)
    put!(task_interthread_channels[Goby.main_task_id], (:cxx_receive, layer, type_name, scheme, group, bytes))
end


function subscribe_interthread(task_id::TaskID, group, callback::Function)
    Threads.lock(group_interthread_channels_lock) do
        if !haskey(Threads.task_local_storage(), :callbacks)
            task_local_storage(:callbacks, Dict{String, Vector{Function}}())
        end
        task_callbacks = task_local_storage(:callbacks)
        
        group_callbacks = get!(task_callbacks, group) do
            Vector{Function}()
        end
        
        push!(group_callbacks, callback)
        
        group_interthread_channel = get!(group_interthread_channels, group) do
            Vector{Channel}()
        end

        push!(group_interthread_channel, task_interthread_channels[task_id])
    end
            
    println("Subscribed $task_id to $group")
end
    
function run(goby_app, main_module, task_modules)
    tasks = Vector{Task}()
    Goby.is_multithreaded = true

    # 1. create channels
    for task_module in task_modules
        task_id = TaskID(Goby.next_task_id.id)
        task_module_to_id[task_module] = task_id
        task_interthread_channels[task_id] = Channel(Goby.channel_size)
        Goby.next_task_id = TaskID(Goby.next_task_id.id + 1)
    end
    
    task_module_to_id[main_module] = main_task_id
    Goby.cxx_task_id = TaskID(Goby.next_task_id.id)        
    task_interthread_channels[Goby.cxx_task_id] = Channel(Goby.channel_size)
    
    
    # 2. spawn tasks
    for task_module in task_modules
        push!(tasks, Goby.task_spawn(task_module))
        
        if haskey(task_module.cfg, :loop_function)
            push!(tasks, Goby.loop_timer(task_module))
        end
    end
    
    push!(tasks, ThreadPools.@tspawnat cxx_task_id.id Goby.cxx_run(goby_app))
    println("Spawning main run() as task ID $cxx_task_id")
    
    if haskey(main_module.cfg, :loop_function)
        push!(tasks, Goby.loop_timer(main_module))
    end
    
    
    while true
        Goby.task_channel_check(main_task_id)
        for task in tasks
            if istaskdone(task)
                fetch(task)
            end
        end
    end
end


function task_spawn(task_module::Module)
    task_id = task_module_to_id[task_module]
    println("Spawning $run as task ID $task_id")

    runner = (id, mod::Module) -> begin
        mod.run(id)
        while true
            Goby.task_channel_check(id)
        end
    end
    
    t = ThreadPools.@tspawnat task_id.id runner(task_id, task_module)
    println("Task: $t")
    return t
end


function loop_timer(task_module::Module)
    task_id = task_module_to_id[task_module]

    timer = (id, mod::Module) -> begin
        while true
            put!(task_interthread_channels[id], (:loop, mod.cfg[:loop_function]))
            sleep(1/mod.cfg[:loop_frequency])
#            println("Loop timer: $task_id")
        end
    end

    t = ThreadPools.@tspawnat timer_task_id.id timer(task_id, task_module)
    return t
end    

function task_channel_check(task_id::TaskID)
    packet = take!(task_interthread_channels[task_id])

#    println("Task $task_id received packet $packet")
    type = packet[1]
    if type == :interthread_receive
        task_callbacks = task_local_storage(:callbacks)
        group = packet[2]
        msg = packet[3]
        if haskey(task_callbacks, group)
            callbacks = task_callbacks[group]
            for callback in callbacks
                callback(task_id, msg)
            end
        end
    elseif type == :loop
        loop_func = packet[2]
        loop_func(task_id)
    elseif type == :cxx_publish
        app = packet[2]
        layer = packet[3]
        group = packet[4]
        msg = packet[5]
        publish(app, layer, group, msg)
    elseif type == :cxx_receive
        layer = packet[2]
        type_name = packet[3]
        scheme = packet[4]
        group = packet[5]
        bytes = packet[6]
        receive_dereferenced(layer, type_name, scheme, group, bytes)
    end    
end

end # module Goby
