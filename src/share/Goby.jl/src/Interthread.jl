# INTERTHREAD delivery between the task modules of MultiThread.run(). Included into MultiThread.
#
# Each task has one inbox, guarded by one condition. It holds the task's mailbox (loop ticks and
# traffic to and from the C++ application) and one bounded queue per INTERTHREAD subscription the
# task made. The task loop takes one item at a time from these sources in turn.

const OVERFLOW_POLICIES = (:block, :drop_oldest, :drop_newest, :error)
const DEFAULT_CAPACITY = 50

mutable struct TaskInbox
    const cond::Threads.Condition
    const packets::Vector{Any}
    const packet_capacity::Int
    # Subscription, which is defined after this
    const subscriptions::Vector{Any}
    # the source to look at first next time: 0 is the mailbox, i is subscriptions[i]
    cursor::Int
end

TaskInbox(packet_capacity::Int) = TaskInbox(Threads.Condition(), Any[], packet_capacity, Any[], 0)

mutable struct Subscription
    const group::String
    const callback::Function
    const task_id::TaskID
    const capacity::Int
    const overflow::Symbol
    const inbox::TaskInbox
    const queue::Vector{Any}
    delivered::Int
    dropped::Int
    warned::Bool
end

# group -> every subscription to it, in any task
const interthread_registry = Dict{String,Vector{Subscription}}()
const interthread_registry_lock = ReentrantLock()

"""
    InterthreadOverflowError

Thrown by `Goby.publish` on `Goby.INTERTHREAD` when a subscription with `overflow = :error` is
full. The message has been delivered to every other subscriber of the group.
"""
struct InterthreadOverflowError <: Exception
    group::String
    tasks::Vector{Symbol}
end

function Base.showerror(io::IO, e::InterthreadOverflowError)
    print(io, "InterthreadOverflowError: the subscription to \"$(e.group)\" in ",
          join(e.tasks, ", "), " is full and its overflow policy is :error")
end

"""
    InterthreadDeadlockError

Thrown instead of waiting forever when a task publishes on `Goby.INTERTHREAD` to its own full
subscription with `overflow = :block`: only that task drains the queue.
"""
struct InterthreadDeadlockError <: Exception
    group::String
    task::Symbol
end

function Base.showerror(io::IO, e::InterthreadDeadlockError)
    print(io, "InterthreadDeadlockError: task $(e.task) published to its own full subscription ",
          "to \"$(e.group)\", which only it can drain. Raise its capacity, or use an overflow ",
          "policy other than :block.")
end

function post!(inbox::TaskInbox, packet)
    lock(inbox.cond) do
        while length(inbox.packets) >= inbox.packet_capacity
            wait(inbox.cond)
        end
        push!(inbox.packets, packet)
        notify(inbox.cond)
    end
end

# Called with inbox.cond held. Takes one item, looking first at the source after the one last
# taken from, so that a busy group cannot starve a quiet one or the mailbox.
function take_ready!(inbox::TaskInbox)
    nsources = 1 + length(inbox.subscriptions)
    for offset in 0:nsources-1
        source = mod(inbox.cursor + offset, nsources)
        if source == 0
            isempty(inbox.packets) && continue
            inbox.cursor = 1
            packet = popfirst!(inbox.packets)
            notify(inbox.cond)
            return (:packet, packet)
        else
            sub = inbox.subscriptions[source]
            isempty(sub.queue) && continue
            inbox.cursor = source + 1
            msg = popfirst!(sub.queue)
            sub.delivered += 1
            notify(inbox.cond)
            return (:message, sub, msg)
        end
    end
    return nothing
end

function take_next!(inbox::TaskInbox)
    lock(inbox.cond) do
        while true
            item = take_ready!(inbox)
            item === nothing || return item
            wait(inbox.cond)
        end
    end
end

function is_ready(inbox::TaskInbox)
    lock(inbox.cond) do
        !isempty(inbox.packets) || any(sub -> !isempty(sub.queue), inbox.subscriptions)
    end
end

function inbox_for(task_id::TaskID)
    inbox = get(task_inboxes, task_id, nothing)
    if inbox === nothing
        throw(ArgumentError("INTERTHREAD is used from thread $(task_id.id), which runs no Goby " *
                            "task: use it from start(), loop() or a callback of Main or a task module"))
    end
    return inbox
end

function check_delivery_settings(capacity, overflow)
    if !(capacity isa Integer) || capacity < 1
        throw(ArgumentError("INTERTHREAD capacity must be a positive integer, got $(repr(capacity))"))
    end
    if !(overflow in OVERFLOW_POLICIES)
        throw(ArgumentError("INTERTHREAD overflow must be one of $(join(repr.(OVERFLOW_POLICIES), ", ")), " *
                            "got $(repr(overflow))"))
    end
end

function subscribe_interthread(task_id::TaskID, group, callback::Function;
                               capacity = nothing, overflow = nothing)
    group = string(group)
    capacity, overflow = Goby.interthread_subscription_settings(task_name(task_id), group, callback,
                                                                capacity, overflow)
    check_delivery_settings(capacity, overflow)

    inbox = inbox_for(task_id)
    sub = Subscription(group, callback, task_id, Int(capacity), overflow, inbox, Any[], 0, 0, false)
    lock(inbox.cond) do
        push!(inbox.subscriptions, sub)
    end
    lock(interthread_registry_lock) do
        push!(get!(Vector{Subscription}, interthread_registry, group), sub)
    end
    return sub
end

function publish_interthread(task_id::TaskID, group, msg)
    group = string(group)
    Goby.check_interthread_publish(task_name(task_id), group, msg)

    # nothing waits with the registry held, so a full subscription cannot stall other groups
    subs = lock(interthread_registry_lock) do
        copy(get(interthread_registry, group, Subscription[]))
    end

    blocked = Subscription[]
    overflowed = Subscription[]
    dropped = Subscription[]
    for sub in subs
        lock(sub.inbox.cond) do
            if length(sub.queue) < sub.capacity
                push!(sub.queue, msg)
                notify(sub.inbox.cond)
            elseif sub.overflow == :block
                push!(blocked, sub)
            else
                sub.dropped += 1
                if sub.overflow == :drop_oldest
                    popfirst!(sub.queue)
                    push!(sub.queue, msg)
                end
                sub.overflow == :error && push!(overflowed, sub)
                if sub.overflow != :error && !sub.warned
                    sub.warned = true
                    push!(dropped, sub)
                end
            end
        end
    end

    for sub in dropped
        @warn "INTERTHREAD subscription to \"$(sub.group)\" in task $(task_name(sub.task_id)) " *
              "is full (capacity $(sub.capacity)) and is dropping messages ($(repr(sub.overflow)))"
    end

    # offered to every subscriber first, so a stalled one delays only its publishers
    for sub in blocked
        if sub.task_id == task_id
            throw(InterthreadDeadlockError(group, task_name(task_id)))
        end
        lock(sub.inbox.cond) do
            while length(sub.queue) >= sub.capacity
                wait(sub.inbox.cond)
            end
            push!(sub.queue, msg)
            notify(sub.inbox.cond)
        end
    end

    if !isempty(overflowed)
        throw(InterthreadOverflowError(group, [task_name(sub.task_id) for sub in overflowed]))
    end
end

function interthread_subscriptions()
    lock(interthread_registry_lock) do
        reduce(vcat, values(interthread_registry); init = Subscription[])
    end
end

function interthread_stats()
    [lock(sub.inbox.cond) do
         (group = sub.group, task = task_name(sub.task_id), capacity = sub.capacity,
          overflow = sub.overflow, queued = length(sub.queue), delivered = sub.delivered,
          dropped = sub.dropped)
     end for sub in interthread_subscriptions()]
end
