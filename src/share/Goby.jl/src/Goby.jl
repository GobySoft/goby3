module Goby

# Load the module and generate the functions
using CxxWrap
using ProtoBuf

export publish, subscribe

# Protobuf publish
# TODO: add more schemes as additional publish functions
function publish(app, layer, group, msg::AbstractProtoBufMessage)
    layer_int::Int32 = Int32(layer)
    scheme = Goby.PROTOBUF
    io = IOBuffer()
    e = ProtoEncoder(io);
    encode(e, msg)
    bytes = take!(io)
    vec = StdVector{UInt8}(bytes)
    type_name = string(nameof(typeof(msg)))
    Goby.cxx_publish(app, layer_int, type_name, scheme, group, vec)
end

callbacks=Dict{Int, Dict{Int, Dict{String, Dict{String, Function}}}}()

function pb_name_from_callback(callback::Function)
    return string(nameof(pb_type_from_callback(callback)))
end

function pb_type_from_callback(callback::Function)
    return methods(callback)[1].sig.parameters[2]
end

function subscribe(app, layer, group, callback::Function; scheme = Goby.NULL_SCHEME, type_name::String = "")
    inferred_scheme = scheme
    inferred_type_name::String = type_name
    layer_int::Int32 = Int32(layer)
    
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
    lvl1 = get!(callbacks, layer_int) do
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
    
    println("Received message $(type_name) (Scheme: $(scheme)) on group $(group)")
    if scheme == Goby.PROTOBUF
        dvec::StdVector{UInt8} = CxxWrap.dereference_argument(vec)
        bytes::Vector{UInt8} = reinterpret(UInt8, collect(dvec))
        callback = callbacks[layer][scheme][type_name][group]
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

end # module Goby
