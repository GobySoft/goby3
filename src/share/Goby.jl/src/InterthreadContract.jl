# The INTERTHREAD contract: what each task publishes and subscribes to, declared in the
# interthread: section of interface.yml and written here by goby_gen_julia. Once declared,
# every INTERTHREAD publication and subscription is held to it.

"""
    InterthreadContractError

Thrown when an INTERTHREAD publication or subscription disagrees with the contract declared in
`interface.yml`, or when the contract itself cannot be satisfied.
"""
struct InterthreadContractError <: Exception
    msg::String
end

Base.showerror(io::IO, e::InterthreadContractError) = print(io, "InterthreadContractError: ", e.msg)

const NECESSITIES = (:required, :recommended, :optional)
const CONTRACT_PUBLISH_KEYS = (:group, :type)
const CONTRACT_SUBSCRIBE_KEYS = (:group, :type, :necessity, :capacity, :overflow)

mutable struct ContractEntry
    const kind::Symbol
    # nothing: any task
    const task::Union{Symbol,Nothing}
    const group::String
    # a Type, or its name as written in interface.yml
    const type_spec::Any
    const necessity::Symbol
    const capacity::Int
    const overflow::Symbol
    type::Any
    made::Bool
end

const interthread_contract_entries = ContractEntry[]
const interthread_contract_lock = ReentrantLock()
const interthread_contract_flag = Ref(false)
const interthread_contract_resolved = Ref(false)

interthread_contract_declared() = interthread_contract_flag[]

contract_entries_of(x::NamedTuple) = (x,)
contract_entries_of(x) = x

function add_contract_entry!(kind::Symbol, task, entry::NamedTuple)
    allowed = kind == :publish ? CONTRACT_PUBLISH_KEYS : CONTRACT_SUBSCRIBE_KEYS
    for key in keys(entry)
        key in allowed || throw(ArgumentError("unknown key '$(key)' in an INTERTHREAD $(kind) " *
                                              "entry; supported keys are $(join(allowed, ", "))"))
    end
    for key in (:group, :type)
        haskey(entry, key) || throw(ArgumentError("an INTERTHREAD $(kind) entry needs '$(key)'"))
    end

    necessity = Symbol(get(entry, :necessity, :optional))
    necessity in NECESSITIES ||
        throw(ArgumentError("necessity must be one of $(join(NECESSITIES, ", ")), got $(necessity)"))
    capacity = get(entry, :capacity, MultiThread.DEFAULT_CAPACITY)
    overflow = Symbol(get(entry, :overflow, :block))
    MultiThread.check_delivery_settings(capacity, overflow)

    push!(interthread_contract_entries,
          ContractEntry(kind, task === nothing ? nothing : Symbol(task), string(entry.group),
                        entry.type, necessity, Int(capacity), overflow, nothing, false))
end

"""
    interthread_contract(; task = nothing, publish = (), subscribe = ())

Declares what `task` (a task module's name, or `:Main`; `nothing` for any task) publishes and
subscribes to on `Goby.INTERTHREAD`. Each of `publish` and `subscribe` is one named tuple or a
collection of them, with `group` and `type`; a subscription also takes `necessity`, `capacity`
and `overflow`.

`goby_gen_julia` writes these calls from the `interthread:` section of `interface.yml`. Once any
call is made, an INTERTHREAD publication or subscription the contract does not declare throws an
[`InterthreadContractError`](@ref).
"""
function interthread_contract(; task = nothing, publish = (), subscribe = ())
    lock(interthread_contract_lock) do
        interthread_contract_flag[] = true
        interthread_contract_resolved[] = false
        for entry in contract_entries_of(publish)
            add_contract_entry!(:publish, task, entry)
        end
        for entry in contract_entries_of(subscribe)
            add_contract_entry!(:subscribe, task, entry)
        end
    end
    return nothing
end

function resolve_contract_type(spec)
    spec isa Type && return spec
    name = replace(string(spec), "::" => ".")
    value = Main
    for part in split(name, ".")
        symbol = Symbol(part)
        if !(value isa Module) || !isdefined(value, symbol)
            throw(InterthreadContractError("type '$(spec)' in interface.yml is not defined: " *
                                           "define or include it before calling Goby.run"))
        end
        value = getfield(value, symbol)
    end
    value isa Type ||
        throw(InterthreadContractError("'$(spec)' in interface.yml names a $(typeof(value)), not a type"))
    return value
end

describe_task(task) = task === nothing ? "any task" : "task $(task)"

"""
    resolve_interthread_contract!()

Resolves the type names in the contract and checks that, on each group, every published type can
be delivered to every subscription. Called by `Goby.run`.
"""
function resolve_interthread_contract!()
    lock(interthread_contract_lock) do
        interthread_contract_resolved[] && return
        for entry in interthread_contract_entries
            entry.type = resolve_contract_type(entry.type_spec)
        end
        for pub in interthread_contract_entries, sub in interthread_contract_entries
            pub.kind == :publish && sub.kind == :subscribe && pub.group == sub.group || continue
            if !(pub.type <: sub.type)
                throw(InterthreadContractError(
                    "on group \"$(pub.group)\", $(describe_task(pub.task)) publishes " *
                    "$(pub.type), which cannot be delivered to the $(sub.type) subscription of " *
                    "$(describe_task(sub.task))"))
            end
        end
        interthread_contract_resolved[] = true
    end
end

function contract_entries_for(kind::Symbol, group::String)
    resolve_interthread_contract!()
    filter(e -> e.kind == kind && e.group == group, interthread_contract_entries)
end

matches_task(entry::ContractEntry, task::Symbol) = entry.task === nothing || entry.task == task

function undeclared_error(kind::Symbol, task::Symbol, group::String, on_group)
    verb = kind == :publish ? "publishes" : "subscribes"
    if isempty(on_group)
        return InterthreadContractError(
            "task $(task) $(verb) on INTERTHREAD group \"$(group)\", which interface.yml does " *
            "not declare: add it under interthread: $(verb)")
    end
    declared = join(unique(describe_task(e.task) for e in on_group), ", ")
    return InterthreadContractError(
        "task $(task) $(verb) on INTERTHREAD group \"$(group)\", which interface.yml declares " *
        "only for $(declared): give the entry thread: $(task), or leave thread out")
end

function check_interthread_publish(task::Symbol, group::String, msg)
    interthread_contract_declared() || return
    on_group = contract_entries_for(:publish, group)
    mine = filter(e -> matches_task(e, task), on_group)
    isempty(mine) && throw(undeclared_error(:publish, task, group, on_group))
    if !any(e -> msg isa e.type, mine)
        throw(InterthreadContractError(
            "task $(task) published a $(typeof(msg)) on \"$(group)\", which interface.yml " *
            "declares as $(join(unique(e.type for e in mine), " or "))"))
    end
end

# capacity and overflow for a new subscription: the contract's, once one is declared
function interthread_subscription_settings(task::Symbol, group::String, callback, capacity, overflow)
    if !interthread_contract_declared()
        return (something(capacity, MultiThread.DEFAULT_CAPACITY), something(overflow, :block))
    end

    on_group = contract_entries_for(:subscribe, group)
    mine = filter(e -> matches_task(e, task), on_group)
    isempty(mine) && throw(undeclared_error(:subscribe, task, group, on_group))

    if capacity !== nothing || overflow !== nothing
        throw(InterthreadContractError(
            "the subscription of task $(task) to \"$(group)\" is declared in interface.yml, so " *
            "its capacity and overflow are set there rather than in Goby.subscribe"))
    end

    index = findfirst(e -> hasmethod(callback, Tuple{e.type}), mine)
    if index === nothing
        throw(InterthreadContractError(
            "$(callback) cannot take $(join(unique(e.type for e in mine), " or ")), which " *
            "interface.yml declares for the subscription of task $(task) to \"$(group)\""))
    end
    entry = mine[index]
    lock(interthread_contract_lock) do
        entry.made = true
    end
    return (entry.capacity, entry.overflow)
end

function report_unmade_interthread_subscriptions()
    interthread_contract_declared() || return
    unmade = lock(interthread_contract_lock) do
        filter(e -> e.kind == :subscribe && !e.made, interthread_contract_entries)
    end
    for entry in unmade
        @warn "interface.yml declares a $(entry.necessity) INTERTHREAD subscription of " *
              "$(describe_task(entry.task)) to \"$(entry.group)\", but no task made it during start()"
    end
end

"""
    interthread_stats()

One named tuple per INTERTHREAD subscription: `group`, `task`, `capacity`, `overflow`, and the
number of messages `queued`, `delivered` and `dropped` so far.
"""
interthread_stats() = MultiThread.interthread_stats()
