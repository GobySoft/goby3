module Goby

# Load the module and generate the functions
using CxxWrap
using ProtoBuf

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
# Multi-threaded: use separate thread (cxx_task_id is updated below after
# spawning child tasks)
cxx_task_id = TaskID(1)

is_multithreaded = false

include("GobyMultiThread.jl")

# main task Protobuf publish
# TODO: add more schemes as additional publish functions
function publish(app, layer, group, msg::AbstractProtoBufMessage)
    layer_int::Int32 = Int32(layer)
    
    if layer_int == Int32(Goby.INTERTHREAD)
        MultiThread.publish_interthread(main_task_id, group, msg)
        return
    end
    
    if Goby.is_multithreaded && Threads.threadid() != Goby.cxx_task_id.id
        MultiThread.publish_forward_interprocess(app, layer, group, msg)
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
        MultiThread.publish_interthread(main_task_id, group, msg)
        return
    end

    msg_type = typeof(msg)
    throw(MethodError("publish not support for layer $layer with message type $msg_type"))
end

# child tasks (interthread)
function publish(task_id::TaskID, layer, group, msg)
    layer_int::Int32 = Int32(layer)
    if layer_int == Int32(Goby.INTERTHREAD)
        MultiThread.publish_interthread(task_id, group, msg)
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
        MultiThread.subscribe_interthread(task_id, group, callback)
        return
    end

    throw(MethodError("subscribe not support for layer $layer with task_id"))
end
                  

function subscribe(app, layer, group, callback::Function; scheme = Goby.NULL_SCHEME, type_name::String = "")
    layer_int::Int32 = Int32(layer)

    if layer_int == Int32(Goby.INTERTHREAD)        
        MultiThread.subscribe_interthread(main_task_id, group, callback)
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
        MultiThread.receive_forward_interprocess(layer, type_name, scheme, group, bytes)
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
    if isdefined(Main, :goby_cfg) && haskey(Main.goby_cfg, :loop_frequency)
        Goby.cxx_set_loop_frequency_hertz(goby_app, Main.goby_cfg[:loop_frequency])
    end
    Goby.cxx_run(goby_app)
end

function cxx_loop()
    if Goby.is_multithreaded
        MultiThread.cxx_loop()
    else
        # Julia loop is directly called from C++ loop()
        Main.goby_cfg[:loop_function]()
    end
end


end # module Goby
