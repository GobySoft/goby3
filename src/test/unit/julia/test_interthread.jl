# Tests INTERTHREAD delivery between Julia tasks: the per-subscription queues, their overflow
# policies, and the contract declared from interface.yml.
#
# INTERTHREAD never reaches the C++ application, so this drives Goby.MultiThread directly and
# needs neither the generated library nor gobyd. Each "task" is an inbox registered the way
# MultiThread.run() registers a task module's; publications name the publishing task explicitly,
# and the test drains a task's inbox itself with task_channel_check(), so that what is queued,
# dropped and delivered can be checked at each step.
#
# Run as: julia -t 4 test_interthread.jl <Goby.jl project directory>

using Pkg
Pkg.activate(ARGS[1], io = devnull)

using Goby
using Test

const MT = Goby.MultiThread

function make_task(id::Int, name::Symbol)
    task_id = MT.TaskID(id)
    MT.task_inboxes[task_id] = MT.TaskInbox(MT.channel_size)
    MT.task_names[task_id] = name
    return task_id
end

const PUBLISHER = make_task(3, :PublisherTask)
const CONSUMER = make_task(4, :ConsumerTask)
const OTHER = make_task(5, :OtherTask)

queued(sub) = lock(() -> copy(sub.queue), sub.inbox.cond)

# runs everything waiting in a task's inbox, as its loop would
function drain!(task_id)
    while MT.is_ready(MT.task_inboxes[task_id])
        MT.task_channel_check(task_id)
    end
end

# true once f() is, or false after a few seconds
function eventually(f; timeout = 5.0)
    deadline = time() + timeout
    while time() < deadline
        f() && return true
        sleep(0.01)
    end
    return false
end

@testset "INTERTHREAD delivery" begin

    @testset "drop_oldest keeps the newest messages and never blocks" begin
        received = Int[]
        sub = MT.subscribe_interthread(CONSUMER, "drop_oldest", m -> push!(received, m);
                                       capacity = 3, overflow = :drop_oldest)
        for i in 1:5
            MT.publish_interthread(PUBLISHER, "drop_oldest", i)
        end
        @test queued(sub) == [3, 4, 5]
        @test sub.dropped == 2

        drain!(CONSUMER)
        @test received == [3, 4, 5]
        @test sub.delivered == 3
    end

    @testset "drop_newest keeps the oldest messages" begin
        received = Int[]
        sub = MT.subscribe_interthread(CONSUMER, "drop_newest", m -> push!(received, m);
                                       capacity = 3, overflow = :drop_newest)
        for i in 1:5
            MT.publish_interthread(PUBLISHER, "drop_newest", i)
        end
        drain!(CONSUMER)
        @test received == [1, 2, 3]
        @test sub.dropped == 2
    end

    @testset "error delivers to the other subscribers, then throws" begin
        strict = MT.subscribe_interthread(CONSUMER, "error", m -> nothing;
                                          capacity = 1, overflow = :error)
        relaxed = MT.subscribe_interthread(OTHER, "error", m -> nothing; capacity = 10)

        MT.publish_interthread(PUBLISHER, "error", 1)
        @test_throws Goby.InterthreadOverflowError MT.publish_interthread(PUBLISHER, "error", 2)
        @test queued(strict) == [1]
        @test queued(relaxed) == [1, 2]
        @test strict.dropped == 1

        drain!(CONSUMER)
        drain!(OTHER)
    end

    # The registry is not held while a publisher waits, so a full subscription delays only the
    # publishers of its own group. Before, one full mailbox stalled every INTERTHREAD publish and
    # subscribe in the process.
    @testset "block waits for room, and only on its own group" begin
        received = Int[]
        sub = MT.subscribe_interthread(CONSUMER, "block", m -> push!(received, m); capacity = 2)
        other = MT.subscribe_interthread(OTHER, "block_other", m -> nothing)

        MT.publish_interthread(PUBLISHER, "block", 1)
        MT.publish_interthread(PUBLISHER, "block", 2)
        waiting = Threads.@spawn MT.publish_interthread(PUBLISHER, "block", 3)

        sleep(0.2)
        @test !istaskdone(waiting)

        MT.publish_interthread(PUBLISHER, "block_other", :unaffected)
        @test queued(other) == [:unaffected]

        MT.task_channel_check(CONSUMER)
        @test eventually(() -> istaskdone(waiting))
        @test queued(sub) == [2, 3]

        drain!(CONSUMER)
        drain!(OTHER)
        @test received == [1, 2, 3]
        @test sub.dropped == 0
    end

    @testset "publishing to your own full subscription throws instead of hanging" begin
        MT.subscribe_interthread(CONSUMER, "self", m -> nothing; capacity = 1)
        MT.publish_interthread(CONSUMER, "self", 1)
        @test_throws Goby.InterthreadDeadlockError MT.publish_interthread(CONSUMER, "self", 2)
        drain!(CONSUMER)
    end

    # Each subscription had the task's one channel pushed onto the group's list, so a task that
    # subscribed twice received each message twice, and ran every callback for each.
    @testset "subscribing twice runs each callback once per message" begin
        first_calls = Ref(0)
        second_calls = Ref(0)
        MT.subscribe_interthread(CONSUMER, "twice", m -> (first_calls[] += 1))
        MT.subscribe_interthread(CONSUMER, "twice", m -> (second_calls[] += 1))

        MT.publish_interthread(PUBLISHER, "twice", :msg)
        drain!(CONSUMER)
        @test first_calls[] == 1
        @test second_calls[] == 1
    end

    @testset "a task serves its subscriptions and mailbox in turn" begin
        order = Symbol[]
        MT.subscribe_interthread(CONSUMER, "busy", m -> push!(order, :busy); capacity = 100)
        MT.subscribe_interthread(CONSUMER, "quiet", m -> push!(order, :quiet))

        for i in 1:10
            MT.publish_interthread(PUBLISHER, "busy", i)
        end
        MT.publish_interthread(PUBLISHER, "quiet", 1)
        MT.post!(MT.task_inboxes[CONSUMER], (:loop, () -> push!(order, :loop)))

        drain!(CONSUMER)
        @test length(order) == 12
        # neither the quiet group nor the loop waits behind all ten busy messages
        @test findfirst(==(:quiet), order) <= 3
        @test findfirst(==(:loop), order) <= 3
    end

    @testset "stats" begin
        sub = MT.subscribe_interthread(CONSUMER, "stats", m -> nothing; capacity = 2,
                                       overflow = :drop_newest)
        for i in 1:3
            MT.publish_interthread(PUBLISHER, "stats", i)
        end
        MT.task_channel_check(CONSUMER)

        stats = only(filter(s -> s.group == "stats", Goby.interthread_stats()))
        @test stats.task == :ConsumerTask
        @test stats.capacity == 2
        @test stats.overflow == :drop_newest
        @test stats.queued == 1
        @test stats.delivered == 1
        @test stats.dropped == 1
        drain!(CONSUMER)
    end

    @testset "delivery settings are checked" begin
        @test_throws ArgumentError MT.subscribe_interthread(CONSUMER, "bad", m -> nothing; capacity = 0)
        @test_throws ArgumentError MT.subscribe_interthread(CONSUMER, "bad", m -> nothing;
                                                            overflow = :sometimes)
        @test_throws ArgumentError MT.subscribe_interthread(MT.TaskID(99), "bad", m -> nothing)
    end
end

struct ExampleWork
    id::Int
end

struct OtherWork end

abstract type Report end
struct DepthReport <: Report
    depth::Float64
end

@testset "INTERTHREAD contract" begin
    # as written by goby_gen_julia from interface.yml: type names are strings resolved from Main
    Goby.interthread_contract(task = :PublisherTask,
                              publish = (group = "work", type = "ExampleWork"))
    Goby.interthread_contract(task = :ConsumerTask,
                              subscribe = (group = "work", type = "ExampleWork", capacity = 2,
                                           overflow = :drop_oldest, necessity = :required))
    Goby.interthread_contract(publish = (group = "report", type = "DepthReport"),
                              subscribe = [(group = "report", type = "Report")])
    Goby.resolve_interthread_contract!()

    @testset "a declared subscription takes its settings from the contract" begin
        sub = MT.subscribe_interthread(CONSUMER, "work", (w::ExampleWork) -> nothing)
        @test sub.capacity == 2
        @test sub.overflow == :drop_oldest
        MT.publish_interthread(PUBLISHER, "work", ExampleWork(1))
        drain!(CONSUMER)
    end

    @testset "a subtype may be delivered to a supertype subscription" begin
        MT.subscribe_interthread(OTHER, "report", (r::Report) -> nothing)
        MT.publish_interthread(PUBLISHER, "report", DepthReport(10.0))
        drain!(OTHER)
    end

    @testset "what the contract does not declare throws" begin
        E = Goby.InterfaceContractError
        @test_throws E MT.publish_interthread(PUBLISHER, "undeclared", 1)
        @test_throws E MT.publish_interthread(PUBLISHER, "work", OtherWork())
        @test_throws E MT.publish_interthread(OTHER, "work", ExampleWork(2))
        @test_throws E MT.subscribe_interthread(CONSUMER, "undeclared", m -> nothing)
        @test_throws E MT.subscribe_interthread(OTHER, "work", (w::ExampleWork) -> nothing)
        @test_throws E MT.subscribe_interthread(CONSUMER, "work", (w::ExampleWork) -> nothing;
                                                capacity = 10)
        # the callback cannot take what the contract delivers
        @test_throws E MT.subscribe_interthread(CONSUMER, "work", (w::OtherWork) -> nothing)
    end

    @testset "the contract itself is checked when it is resolved" begin
        saved = copy(Goby.interthread_contract_entries)
        try
            empty!(Goby.interthread_contract_entries)
            Goby.interthread_contract(publish = (group = "g", type = "NoSuchType"))
            @test_throws Goby.InterfaceContractError Goby.resolve_interthread_contract!()

            empty!(Goby.interthread_contract_entries)
            Goby.interthread_contract(publish = (group = "g", type = "OtherWork"),
                                      subscribe = (group = "g", type = "ExampleWork"))
            @test_throws Goby.InterfaceContractError Goby.resolve_interthread_contract!()

            @test_throws ArgumentError Goby.interthread_contract(
                subscribe = (group = "g", type = "ExampleWork", overflow = :sometimes))
            @test_throws ArgumentError Goby.interthread_contract(
                publish = (group = "g", type = "ExampleWork", capacity = 3))
        finally
            empty!(Goby.interthread_contract_entries)
            append!(Goby.interthread_contract_entries, saved)
            Goby.interthread_contract_resolved[] = false
        end
    end

    @testset "an application run as one task cannot use INTERTHREAD" begin
        @test_throws ErrorException MT.check_single_task()
    end
end

# thread: on an interprocess or intermodule entry ties the call to that task. The generated C++
# already rejects calls interface.yml does not declare, so only declared calls are checked.
@testset "outer-layer threads" begin
    interprocess = Int32(1)
    Goby.outer_layer_contract(task = :PublisherTask,
                              publish = [(layer = :interprocess, group = "groups::nav",
                                          type = "goby3_examples::protobuf::NavigationReport")])
    Goby.outer_layer_contract(subscribe = [(layer = :interprocess, group = "groups::status",
                                            type = "project.protobuf.Status")])

    # matched on the message name without its scope, as the C++ glue matches it
    @test Goby.check_outer_layer_call(:publish, :PublisherTask, interprocess, "groups::nav",
                                      "NavigationReport") === nothing
    @test_throws Goby.InterfaceContractError Goby.check_outer_layer_call(
        :publish, :ConsumerTask, interprocess, "groups::nav", "NavigationReport")
    # an entry without thread lets any task make the call
    @test Goby.check_outer_layer_call(:subscribe, :ConsumerTask, interprocess, "groups::status",
                                      "Status") === nothing
    # a call no entry ties to a task is left to the C++ glue
    @test Goby.check_outer_layer_call(:subscribe, :ConsumerTask, interprocess, "groups::nav",
                                      "NavigationReport") === nothing
end
