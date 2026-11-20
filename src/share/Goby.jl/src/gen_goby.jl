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
const SUBSCRIPTION_KEYS = ("group", "scheme", "type", "necessity")
const INTERTHREAD_ENTRY_KEYS = ("group", "scheme", "type", "thread")
const INTERTHREAD_SUBSCRIPTION_KEYS = ("group", "scheme", "type", "thread", "necessity", "capacity", "overflow")
const NECESSITIES = ("required", "recommended", "optional")
const OVERFLOWS = ("block", "drop_oldest", "drop_newest", "error")
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

# NATIVE is a value of the bound language, which only interthread -- never marshalled -- can carry
function check_scheme(scheme, layer = "")
    if layer == "interthread" && scheme == "NATIVE"
        return
    end
    if scheme != "PROTOBUF"
        throw(InvalidInterfaceError(layer == "interthread" ?
            "The supported interthread schemes are 'PROTOBUF' and 'NATIVE'" :
            "The only supported scheme is currently 'PROTOBUF' ('NATIVE' is interthread only)"))
    end
end

const IDENTIFIER = r"^[A-Za-z_][A-Za-z0-9_]*$"

function check_interthread_settings(s, where::String)
    if haskey(s, "thread") && !occursin(IDENTIFIER, string(s["thread"]))
        throw(InvalidInterfaceError("'$(where).thread' must be an identifier, got '$(s["thread"])'"))
    end
    if haskey(s, "capacity") && !(s["capacity"] isa Integer && s["capacity"] >= 1)
        throw(InvalidInterfaceError("'$(where).capacity' must be a positive integer, got '$(s["capacity"])'"))
    end
    if haskey(s, "overflow") && !(string(s["overflow"]) in OVERFLOWS)
        throw(InvalidInterfaceError("'$(where).overflow' is '$(s["overflow"])'. Supported values are: $(join(OVERFLOWS, ", "))"))
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

    # entries rather than macro calls alone: check_unambiguous needs what each one matches on
    publish = []
    subscribe = []
    layer_function=haskey(layer_yaml, "alias") ? layer_yaml["alias"] : layer
    layer_enum=uppercase(layer)
    interthread = layer == "interthread"

    if haskey(layer_yaml, "publishes")
        for p in layer_yaml["publishes"]
            keys_allowed = interthread ? INTERTHREAD_ENTRY_KEYS : ENTRY_KEYS
            check_keys(p, ENTRY_KEYS, keys_allowed, "$(layer).publishes")
            check_scheme(p["scheme"], layer)
            interthread && check_interthread_settings(p, "$(layer).publishes")
            # Julia ::Method sig does not include scoping, so we need to remove it here
            t = to_cpp_scoping(p["type"])
            # an interthread group is the string Goby.jl delivers on, as the generated constant has it
            g = interthread ? string(p["group"]) : to_cpp_scoping(string(p["group"]))
            thread = interthread ? string(get(p, "thread", "")) : ""
            push!(publish, (macro_call = "GOBY_JULIA_IF_PUBLICATION($(p["scheme"]), $(layer_enum), $(layer_function), $(g), \"$(g)\", $(t))",
                            key = (layer, p["scheme"], t, g, thread),
                            accessor = layer_function,
                            layer = layer, group = g, type = t, thread = thread,
                            necessity = "optional", capacity = 0, overflow = ""))
        end
    end

    if haskey(layer_yaml, "subscribes")
        for s in layer_yaml["subscribes"]
            keys_allowed = interthread ? INTERTHREAD_SUBSCRIPTION_KEYS : SUBSCRIPTION_KEYS
            check_keys(s, ENTRY_KEYS, keys_allowed, "$(layer).subscribes")
            check_scheme(s["scheme"], layer)
            interthread && check_interthread_settings(s, "$(layer).subscribes")
            necessity = string(get(s, "necessity", "optional"))
            if !(necessity in NECESSITIES)
                throw(InvalidInterfaceError("'$(layer).subscribes.necessity' is '$(necessity)'. Supported values are: $(join(NECESSITIES, ", "))"))
            end
            t = to_cpp_scoping(s["type"])
            g = interthread ? string(s["group"]) : to_cpp_scoping(string(s["group"]))
            thread = interthread ? string(get(s, "thread", "")) : ""
            push!(subscribe, (macro_call = "GOBY_JULIA_IF_SUBSCRIPTION($(s["scheme"]), $(layer_enum), $(layer_function), $(g), \"$(g)\", $(t), $(uppercase(necessity)))",
                              key = (layer, s["scheme"], t, g, thread),
                              accessor = layer_function,
                              layer = layer, group = g, type = t, thread = thread,
                              necessity = necessity,
                              capacity = Int(get(s, "capacity", 50)),
                              overflow = string(get(s, "overflow", "block"))))
        end
    end

    return (publish, subscribe)
end

# Two entries the generated code could not tell apart. A publication or subscription is matched
# on layer, scheme, type and group; the accessor is not part of that, so two entries agreeing on
# all four compile to two branches with identical conditions, and the second is unreachable
# whichever accessor the application called.
#
# Interthread entries never reach C++, but one thread declaring the same entry twice is as
# ambiguous to the contract, so they are checked with their thread as part of the key.
function check_unambiguous(entries, layer_kind::String)
    seen = Dict{Tuple{String,String,String,String,String},String}()
    for entry in entries
        if haskey(seen, entry.key)
            layer, scheme, type_name, group, thread = entry.key
            if layer == "interthread"
                throw(InvalidInterfaceError(
                    "'interthread.$(layer_kind)' declares $(type_name) on group $(group) (scheme " *
                    "$(scheme)) twice for $(isempty(thread) ? "any thread" : "thread $(thread)")"))
            end
            throw(InvalidInterfaceError(
                "'$(layer).$(layer_kind)' declares $(type_name) on group $(group) (scheme $(scheme)) " *
                "twice, as '$(seen[entry.key])' and '$(entry.accessor)'. Only the layer identifies a " *
                "portal across the language boundary, so the two cannot be told apart and the second " *
                "would never be reached."))
        end
        seen[entry.key] = entry.accessor
    end
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

const LAYERS = ("interthread", "interprocess", "intermodule")

function collect_entries(interface_yaml)
    publish = []
    subscribe = []
    for layer in LAYERS
        if haskey(interface_yaml, layer)
            layer_value = interface_yaml[layer]
            for portal in (isa(layer_value, Vector) ? layer_value : [layer_value])
                p, s = collect_layer(layer, portal)
                append!(publish, p)
                append!(subscribe, s)
            end
        end
    end

    check_unambiguous(publish, "publishes")
    check_unambiguous(subscribe, "subscribes")
    return (publish, subscribe)
end

function load_interface(in_yaml::String)
    interface_yaml = YAML.load_file(in_yaml)
    check_top_level_keys(interface_yaml, LAYERS)
    name = application_name(interface_yaml["application"])
    publish, subscribe = collect_entries(interface_yaml)
    return (interface_yaml, name, publish, subscribe)
end

function goby_gen_cpp(in_yaml::String, out_cpp::String, includes; verbose::Bool=false)

    verbose && println("Generating $(out_cpp) from $(in_yaml)")

    interface_yaml, name, publish, subscribe = load_interface(in_yaml)

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

    # interthread is carried between Julia tasks by Goby.jl and never reaches the C++ application
    gen_class(io_out, name,
              String[e.macro_call for e in publish if e.layer != "interthread"],
              String[e.macro_call for e in subscribe if e.layer != "interthread"])
    close(io_out)
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

# the type as Julia names it, which is where the contract looks it up
julia_type_name(t::String) = replace(t, "::" => ".")

julia_task(thread::String) = isempty(thread) ? "nothing" : ":$(thread)"

# One Goby.interthread_contract call per thread. Types are written as names, resolved by
# Goby.run once the application has defined them.
function gen_interthread_contract(io, publish, subscribe)
    pubs = [e for e in publish if e.layer == "interthread"]
    subs = [e for e in subscribe if e.layer == "interthread"]
    for thread in unique(e.thread for e in vcat(pubs, subs))
        args = ["task = $(julia_task(thread))"]
        entries = ["(group = \"$(e.group)\", type = \"$(julia_type_name(e.type))\")"
                   for e in pubs if e.thread == thread]
        isempty(entries) || push!(args, "publish = [\n        " * join(entries, ",\n        ") * "]")
        entries = ["(group = \"$(e.group)\", type = \"$(julia_type_name(e.type))\", " *
                   "necessity = :$(e.necessity), capacity = $(e.capacity), overflow = :$(e.overflow))"
                   for e in subs if e.thread == thread]
        isempty(entries) || push!(args, "subscribe = [\n        " * join(entries, ",\n        ") * "]")
        write(io, "Goby.interthread_contract(" * join(args, ",\n    ") * ")\n")
    end
end

"""
    goby_gen_julia(in_yaml, out_jl; verbose=false)

Writes the Julia side of the interface: the groups an application publishes and subscribes to,
by name rather than as repeated string literals, and one accessor per portal. A group the
generated C++ has no case for already terminates the application through GOBY_JULIA_FAIL when
the call is reached; the constant moves that to an UndefVarError naming the typo.

When the interface declares interthread, the module also declares it to Goby.jl, which then
holds every INTERTHREAD publication and subscription to it.
"""
function goby_gen_julia(in_yaml::String, out_jl::String; verbose::Bool=false)
    verbose && println("Generating $(out_jl) from $(in_yaml)")

    interface_yaml, name, publish, subscribe = load_interface(in_yaml)
    groups = collect_groups(interface_yaml, LAYERS)
    accessors = collect_accessors(interface_yaml, LAYERS)

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

        if haskey(interface_yaml, "interthread")
            write(io, "\n# the interthread contract, which Goby.jl holds every INTERTHREAD call to\n")
            write(io, "Goby.interthread_contract()\n")
            gen_interthread_contract(io, publish, subscribe)
        end

        write(io, "\nend # module $(name)Goby\n")
    end
end

yaml_string(s) = "\"" * escape_string(string(s)) * "\""

"""
    goby_gen_interface(in_yaml, out_yml, target; verbose=false)

Writes the interthread contract as a manifest for `goby_clang_tool -gen -interthread-manifest`,
which adds it to the interface file it writes for `target`. Interthread never reaches the
generated C++, so this is the only way `goby interface` learns of it. With no interthread
section the manifest names the application and nothing else.
"""
function goby_gen_interface(in_yaml::String, out_yml::String, target::String; verbose::Bool=false)
    verbose && println("Generating $(out_yml) from $(in_yaml)")

    interface_yaml, name, publish, subscribe = load_interface(in_yaml)

    open(out_yml, "w") do io
        write(io, "# Generated from $(in_yaml) for goby_clang_tool -interthread-manifest\n")
        write(io, "application: $(yaml_string(target))\n")
        haskey(interface_yaml, "interthread") || return

        task_key(e) = isempty(e.thread) ? "" : "task: $(yaml_string(e.thread)), "
        write(io, "julia_interthread:\n  publishes:\n")
        for e in publish
            e.layer == "interthread" || continue
            write(io, "    - { $(task_key(e))group: $(yaml_string(e.group)), " *
                      "type: $(yaml_string(julia_type_name(e.type))) }\n")
        end
        write(io, "  subscribes:\n")
        for e in subscribe
            e.layer == "interthread" || continue
            write(io, "    - { $(task_key(e))group: $(yaml_string(e.group)), " *
                      "type: $(yaml_string(julia_type_name(e.type))), necessity: $(e.necessity), " *
                      "capacity: $(e.capacity), overflow: $(e.overflow) }\n")
        end
    end
end
