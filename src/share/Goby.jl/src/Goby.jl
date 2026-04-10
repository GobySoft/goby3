module Goby

# Load the module and generate the functions
using CxxWrap
using ProtoBuf

export publish, subscribe

is_multithreaded = false

include("GobyMultiThread.jl")

function goby_type_name(type)
    return string(nameof(type))
#    return replace(string(type), r"^Main\." => "")
end

# main task Protobuf publish
# TODO: add more schemes as additional publish functions
"""i
    publish(app, layer, group, msg)

Publish a message `msg` using this `app` (CxxWrap'd Goby App) to this `layer` (e.g., Goby.INTERPROCESS), using this `group` (string). Current `msg` must be an AbstractProtoBufMessage generated using the ProtoBuf.jl library, unless `layer` is Goby.INTERTHREAD (in which case `msg` can be any Julia type).
"""
function publish(app, layer, group::String, msg::AbstractProtoBufMessage)
    if Goby.is_multithreaded && MultiThread.check_and_publish(app, layer, group, msg)
        return
    end
    
    scheme = Goby.PROTOBUF
    io = IOBuffer()
    e = ProtoEncoder(io);
    encode(e, msg)
    bytes = take!(io)
    vec = StdVector{UInt8}(bytes)
    type_name = goby_type_name(typeof(msg))

    
    threadid=Threads.threadid()

    layer_int::Int32 = Int32(layer)
    Goby.cxx_publish(app, layer_int, type_name, scheme, group, vec)
end

# main task (interthread)
function publish(app, layer, group, msg)
    if Goby.is_multithreaded && MultiThread.check_and_publish(app, layer, group, msg)
        return
    end
    
    msg_type = typeof(msg)
    throw(AssertionError("publish not support for layer $layer with message type $msg_type"))
end

interprocess_callbacks=Dict{Int, Dict{Int, Dict{String, Dict{String, Function}}}}()

function pb_name_from_callback(callback::Function)
    return goby_type_name(pb_type_from_callback(callback))
end

function pb_type_from_callback(callback::Function)
    return methods(callback)[1].sig.parameters[2]
end

"""
    subscribe(app, layer, group, callback::Function; scheme = Goby.NULL_SCHEME, type_name::String = "")

Subscribe to messages using this `app` (CxxWrap'd Goby App) on this `layer` (e.g., Goby.INTERPROCESS), using this `group` (string). When a message is received, call the `callback` function. If `scheme` and `type_name` are not defined, they will be inferred from the type of the first argument of `callback` (currently only supports AbstractProtoBufMessage, unless `layer` is Goby.INTERTHREAD which supports all Julia types).
"""
function subscribe(app, layer, group, callback::Function; scheme = Goby.NULL_SCHEME, type_name::String = "")
    layer_int::Int32 = Int32(layer)

    if MultiThread.check_and_subscribe(layer, group, callback)
        return
    end

    inferred_scheme = scheme
    inferred_type_name::String = type_name
    
    for m in methods(callback)
        sig = Base.unwrap_unionall(m.sig)     # remove type wrappers like UnionAll
        argtypes = sig.parameters
        if length(argtypes) != 2 # parameters includes type of function and arguments
            throw(ArgumentError("Function must have exactly one argument: $callback"))
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

    if Goby.is_multithreaded && MultiThread.check_and_receive(layer, type_name, scheme, group, bytes)
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

"""
    read_cli_cfg()

Reads the contents of the TextFormat file given as the first positional argument on the commandline (e.g., config.pb.cfg) and returns it as a String. This is intended to be passed as the first argument to the CxxWrap'd Goby App (e.g., `goby_app = Goby.JuliaDemo(Goby.read_cli_cfg())`)
"""
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


function cxx_loop()
    if Goby.is_multithreaded
        MultiThread.cxx_loop()
    else
        # Julia loop is directly called from C++ loop()
        Main.goby_cfg[:loop_function]()
    end
end

"""
    run(goby_app, main_module = Main, task_modules = [])

Run the `goby_app` (CxxWrap'd Goby App) using the functions and data in main_module (function `start`, if defined and Dict `goby_cfg`, if defined). If tasks_modules is defined, these are run as child tasks (threads).

The application is run in one thread if tasks_modules is empty, otherwise it uses length(task_modules) + 3 threads.
"""
function run(goby_app, main_module = Main, task_modules = [])
    if length(task_modules) == 0
        # single threaded
        if isdefined(main_module, :goby_cfg) && haskey(main_module.goby_cfg, :loop_frequency)
            Goby.cxx_set_loop_frequency_hertz(goby_app, main_module.goby_cfg[:loop_frequency])
        end
        if isdefined(main_module, :start)
            main_module.start()
        end
        Goby.cxx_run(goby_app)
    else
        # multi threaded
        MultiThread.run(goby_app, main_module, task_modules)
    end
end


"""
    cfg(goby_app)

Reads the Protobuf configuration that the Goby application was initialized with and returns it as Dict
"""
function cfg(goby_app, pb_type)
    dvec::StdVector{UInt8} = CxxWrap.dereference_argument(Goby.cxx_cfg_serialized(goby_app))
    bytes::Vector{UInt8} = reinterpret(UInt8, collect(dvec))
    io = IOBuffer(bytes)
    d::ProtoDecoder = ProtoDecoder(io)
    msg = decode(d, pb_type)
    return msg
end

end # module Goby
