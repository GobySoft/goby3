using YAML
using ProtoBuf

function gen_proto(protos, includes, outdir, stampfile)
    protojl(protos, includes, outdir, common_abstract_type=true, add_kwarg_constructors=true)
    open(stampfile, "w") do io
        write(io, "done")
    end
end

struct InvalidInterfaceError <: Exception
var::String
end

# turns Julia/Protobuf scoping (".") into C++ scoping ("::")
function to_cpp_scoping(s::String)
    occursin("::", s) ? s : replace(s, "." => "::")
end

#function remove_namespaces(s::String)
#    last(split(s, "::"))
#end

const ENTRY_KEYS = ("group", "scheme", "type")
const PORTAL_KEYS = ("alias", "publishes", "subscribes")
const APPLICATION_KEYS = ("name", "cpp_type", "config")
const CONFIG_KEYS = ("scheme", "type")

# a key the format does not define is a typo, not an extension: 'subscribe' for 'subscribes'
# would otherwise generate an application that silently subscribes to nothing
function check_keys(yaml, required, allowed, where::String)
    if !isa(yaml, AbstractDict)
        throw(InvalidInterfaceError("'$(where)' must be a mapping"))
    end
    for key in required
        if !haskey(yaml, key)
            throw(InvalidInterfaceError("Interface file must have '$(where).$(key)' key"))
        end
    end
    for key in keys(yaml)
        if !(key in allowed)
            throw(InvalidInterfaceError("Unknown key '$(where).$(key)'. Supported keys are: $(join(allowed, ", "))"))
        end
    end
end

function check_scheme(scheme)
    if scheme != "PROTOBUF"
        throw(InvalidInterfaceError("The only supported scheme is currently 'PROTOBUF'"))
    end
end

# the interface.yml format is shared with the other Goby language bindings (see
# share/goby/interface/README.md), so reject anything we don't implement rather than
# silently ignoring it
function check_top_level_keys(yaml, layers)
    allowed = Set(["application", layers...])
    for key in keys(yaml)
        if !(key in allowed)
            throw(InvalidInterfaceError("Unknown top-level key '$(key)' in interface file. Supported keys are: $(join(sort(collect(allowed)), ", "))"))
        end
    end
    if !haskey(yaml, "application")
        throw(InvalidInterfaceError("Interface file must have 'application' key"))
    end
end


function collect_layer(layer::String, layer_yaml)
    check_keys(layer_yaml, (), PORTAL_KEYS, layer)

    publish = Vector{String}()
    subscribe = Vector{String}()
    layer_function=haskey(layer_yaml, "alias") ? layer_yaml["alias"] : layer
    layer_enum=uppercase(layer)

    if haskey(layer_yaml, "publishes")
        for p in layer_yaml["publishes"]
            check_keys(p, ENTRY_KEYS, ENTRY_KEYS, "$(layer).publishes")
            check_scheme(p["scheme"])
            # Julia ::Method sig does not include scoping, so we need to remove it here
            t = to_cpp_scoping(p["type"])
            push!(publish, "GOBY_JULIA_IF_PUBLICATION($(p["scheme"]), $(layer_enum), $(layer_function), $(p["group"]), \"$(p["group"])\", $(t))")
        end
    end
    
    if haskey(layer_yaml, "subscribes")
        for s in layer_yaml["subscribes"]
            check_keys(s, ENTRY_KEYS, ENTRY_KEYS, "$(layer).subscribes")
            check_scheme(s["scheme"])
            t = to_cpp_scoping(s["type"])
            push!(subscribe, "GOBY_JULIA_IF_SUBSCRIPTION($(s["scheme"]), $(layer_enum), $(layer_function), $(s["group"]), \"$(s["group"])\", $(t))")
        end
    end
        
    return (publish, subscribe)
end


function gen_includes(io_out::IOStream, includes)
    write(io_out, "#include <goby/middleware/languages/julia/application.h>\n\n")

    for include in includes
        write(io_out, "#include \"$(include)\"\n")
    end
    
    write(io_out, "\n")
end

# it becomes the generated C++ class name, so a name that is not an identifier has to be
# rejected here rather than left to the compiler
function application_name(application_yaml)
    check_keys(application_yaml, APPLICATION_KEYS, APPLICATION_KEYS, "application")
    name = string(application_yaml["name"])
    if !occursin(r"^[A-Za-z_][A-Za-z0-9_]*$", name)
        throw(InvalidInterfaceError("'application.name' must be a valid C++ identifier, got '$(name)'"))
    end
    return name
end

function gen_application_macros(io_out::IOStream, application_yaml)
    name = application_name(application_yaml)
    config_yaml = application_yaml["config"]
    check_keys(config_yaml, CONFIG_KEYS, CONFIG_KEYS, "application.config")
    check_scheme(config_yaml["scheme"])

    c = to_cpp_scoping(config_yaml["type"])
    write(io_out, "#define CONFIG_TYPE $(c)\n")
    t = to_cpp_scoping(application_yaml["cpp_type"])
    write(io_out, "#define APPLICATION_TYPE $(t)<CONFIG_TYPE>\n")
    write(io_out, "#define APPLICATION_NAME $(name)\n")
    write(io_out, "\n")
end

function gen_class(io_out::IOStream, application_name::String,
                   publish::Vector{String}, subscribe::Vector{String})
    write(io_out, """\
class APPLICATION_NAME : public goby::middleware::julia::Application<APPLICATION_TYPE>
{
  public:
    void publish(goby::middleware::julia::Identifier id, const std::vector<std::uint8_t>& bytes)
    {
""")

    for p in publish
        write(io_out, "        "*p*"\n")
    end
        
    write(io_out, """\

        GOBY_JULIA_FAIL("publish")
    }

    void subscribe(goby::middleware::julia::Identifier id, std::string func, std::string module)
    {
""")
    
    for s in subscribe
        write(io_out, "        "*s*"\n")
    end

    write(io_out, """\

        GOBY_JULIA_FAIL("subscribe")
    }
};

GOBY_JULIA_DEFINE_MODULE(APPLICATION_NAME)
""")
end

function goby_gen_cpp(in_yaml::String, out_cpp::String, includes)

    println("Generating $(out_cpp) from $(in_yaml)")
    
    interface_yaml = YAML.load_file(in_yaml)

    layers = ("interthread", "interprocess", "intermodule")
    check_top_level_keys(interface_yaml, layers)

    io_out::IOStream = open(out_cpp, "w");
    write(io_out, """\
// ########################
// #   Goby <--> Julia    #
// #  C++ support library #
// ########################
// This file was autogenerated from
// $(in_yaml)
""")
    gen_includes(io_out, includes)
    gen_application_macros(io_out, interface_yaml["application"])

    publish = Vector{String}()
    subscribe = Vector{String}()
    for layer in layers
        if haskey(interface_yaml, layer)
            layer_value = interface_yaml[layer]
            if isa(layer_value, Vector)
                for entry in layer_value
                    p, s = collect_layer(layer, entry)
                    append!(publish, p)
                    append!(subscribe, s)
                end
            else
                p, s = collect_layer(layer, layer_value)
                append!(publish, p)
                append!(subscribe, s)
            end
        end
    end
    gen_class(io_out, interface_yaml["application"]["name"], publish, subscribe)
end

# Every portal declared on a layer, as (accessor name, layer enum). The accessor is the alias
# where one is given, which is how a project names a layer that has more than one portal.
function collect_accessors(interface_yaml, layers)
    accessors = Vector{Tuple{String,String}}()
    for layer in layers
        haskey(interface_yaml, layer) || continue
        value = interface_yaml[layer]
        for portal in (isa(value, Vector) ? value : [value])
            accessor = haskey(portal, "alias") ? string(portal["alias"]) : layer
            if !any(a -> a[1] == accessor, accessors)
                push!(accessors, (accessor, uppercase(layer)))
            end
        end
    end
    return accessors
end

# Group expressions by their last '::' component, which is what an application writes. A short
# name claimed by two different expressions is dropped rather than guessed at: the expression
# still works as a string.
function collect_groups(interface_yaml, layers)
    groups = Dict{String,String}()
    ambiguous = Set{String}()
    for layer in layers
        haskey(interface_yaml, layer) || continue
        value = interface_yaml[layer]
        for portal in (isa(value, Vector) ? value : [value])
            for key in ("publishes", "subscribes")
                haskey(portal, key) || continue
                for entry in portal[key]
                    expression = string(entry["group"])
                    short = String(last(split(expression, "::")))
                    if haskey(groups, short) && groups[short] != expression
                        push!(ambiguous, short)
                    else
                        groups[short] = expression
                    end
                end
            end
        end
    end
    for short in ambiguous
        delete!(groups, short)
    end
    return groups
end

"""
    goby_gen_julia(in_yaml, out_jl)

Writes the Julia side of the interface: the groups an application publishes and subscribes to,
by name rather than as repeated string literals, and one accessor per portal. Publishing to a
group Goby has no binding for fails at runtime, so the constant is what stops a typo becoming a
message that goes nowhere.
"""
function goby_gen_julia(in_yaml::String, out_jl::String)
    println("Generating $(out_jl) from $(in_yaml)")

    interface_yaml = YAML.load_file(in_yaml)
    layers = ("interthread", "interprocess", "intermodule")
    check_top_level_keys(interface_yaml, layers)

    name = application_name(interface_yaml["application"])
    groups = collect_groups(interface_yaml, layers)
    accessors = collect_accessors(interface_yaml, layers)

    open(out_jl, "w") do io
        write(io, """\
# ########################
# #   Goby <--> Julia    #
# #    Julia interface   #
# ########################
# This file was autogenerated from
# $(in_yaml)

module $(name)Goby

using Goby

const APPLICATION_NAME = "$(name)"

# the interface.yml expression for each group, which is how Goby names it across the boundary
module groups
""")
        for short in sort(collect(keys(groups)))
            write(io, "const $(short) = \"$(groups[short])\"\n")
        end
        write(io, """\
end

""")
        # functions rather than constants: the layer enums arrive with the application library,
        # which is loaded after this file is included
        for (accessor, layer_enum) in accessors
            write(io, "$(accessor)() = Goby.$(layer_enum)\n")
        end
        write(io, """\

end # module $(name)Goby
""")
    end
end
