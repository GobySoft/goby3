module MultiThread

using ..Goby
using ThreadPools

########################
# Interthread          #
# Using Tasks/Inboxes  #
########################

struct TaskID
    id::Int64
end

# run Main module functions on thread 1
main_task_id = TaskID(1)
# run timers for loop, etc. on thread 2
timer_task_id = TaskID(2)
# Multi-threaded: start first non-main child task on thread 3
next_task_id = TaskID(3)

# Single-threaded, using thread 1 for CxxWrap'd class
# Multi-threaded: use separate thread (cxx_task_id is updated below after
# spawning child tasks)
cxx_task_id = nothing

# bounds the mailbox: loop ticks and traffic to and from the C++ application
channel_size = 50

include("Interthread.jl")

# map of module to task_id
task_module_to_id = Dict{Module,TaskID}()
# the name interface.yml gives each task: its module's name
task_names = Dict{TaskID,Symbol}(main_task_id => :Main)
task_inboxes = Dict{TaskID,TaskInbox}()
task_inboxes[MultiThread.main_task_id] = TaskInbox(channel_size)

task_name(task_id::TaskID) = get(task_names, task_id, Symbol("thread", task_id.id))
current_task_id() = TaskID(Threads.threadid())

# which task asked for each C++ subscription, so that a message arriving on the C++ thread is
# delivered to the task that wants it rather than always to Main. Keyed the same way the C++
# side matches: (layer, scheme, type name, group).
const SubscriptionKey = Tuple{Int32,Int,String,String}
cxx_subscriber_tasks = Dict{SubscriptionKey,Vector{TaskID}}()
cxx_subscriber_tasks_lock = Threads.ReentrantLock()

function record_cxx_subscriber(key::SubscriptionKey, task_id::TaskID)
    Threads.lock(cxx_subscriber_tasks_lock) do
        tasks = get!(cxx_subscriber_tasks, key) do
            Vector{TaskID}()
        end
        if !(task_id in tasks)
            push!(tasks, task_id)
        end
    end
end

function cxx_subscriber_inboxes(key::SubscriptionKey)
    Threads.lock(cxx_subscriber_tasks_lock) do
        # anything not registered goes to Main, which is where it went before tasks could subscribe
        task_ids = get(cxx_subscriber_tasks, key, [MultiThread.main_task_id])
        return [task_inboxes[id] for id in task_ids]
    end
end

# Check for and intercept interthread publications
# Return true if we did, false if this isn't our publication
function check_and_publish(app, layer, group, msg)
    layer_int::Int32 = Int32(layer)
    # named up front so that the branches reporting it have it too
    task_id = current_task_id()
    if layer_int == Int32(Goby.INTERTHREAD)
        publish_interthread(task_id, group, msg)
        return true
    elseif Threads.threadid() == MultiThread.cxx_task_id.id
        # this task owns the C++ application, so publish straight through
        return false
    else
        # every other task hands the message to the one that does, which is what a C++ thread's
        # InterProcessForwarder does with its inner interthread transporter
        publish_forward_cxx(app, layer, group, msg)
        return true
    end
end

function check_and_subscribe(layer, group, callback::Function; capacity = nothing,
                             overflow = nothing)
    layer_int::Int32 = Int32(layer)
    if layer_int == Int32(Goby.INTERTHREAD)
        MultiThread.subscribe_interthread(current_task_id(), group, callback;
                                          capacity = capacity, overflow = overflow)
        return true
    end

    if capacity !== nothing || overflow !== nothing
        throw(ArgumentError("capacity and overflow apply to Goby.INTERTHREAD subscriptions only"))
    end

    # every other layer needs the scheme and type inferred first, so it is finished by
    # check_and_cxx_subscribe once Goby.subscribe knows them
    return false
end

# Registers the subscription with C++ on the task that owns the application, and remembers which
# task to hand the messages to. Returns true when it took over the call.
function check_and_cxx_subscribe(app, layer_int::Int32, type_name::String, scheme, group)
    # single threaded: there is one task, and cxx_task_id is not set up
    if !Goby.is_multithreaded
        return false
    end

    task_id = current_task_id()
    record_cxx_subscriber((layer_int, Int(scheme), type_name, string(group)), task_id)

    if Threads.threadid() == MultiThread.cxx_task_id.id
        return false
    end

    post!(task_inboxes[MultiThread.cxx_task_id],
          (:cxx_subscribe, app, layer_int, type_name, scheme, group))
    return true
end

function check_and_receive(layer, type_name, scheme, group, bytes)
    if Threads.threadid() == MultiThread.cxx_task_id.id
        receive_forward_cxx(layer, type_name, scheme, group, bytes)
        return true
    end
    return false
end

# any layer the C++ side handles, not just interprocess: the layer is passed through untouched
function publish_forward_cxx(app, layer, group, msg)
    post!(task_inboxes[MultiThread.cxx_task_id], (:cxx_publish, app, layer, group, msg))
end

function receive_forward_cxx(layer, type_name, scheme, group, bytes)
    for inbox in cxx_subscriber_inboxes((Int32(layer), Int(scheme), string(type_name), string(group)))
        post!(inbox, (:cxx_receive, layer, type_name, scheme, group, bytes))
    end
end

# INTERTHREAD is carried between the task modules of MultiThread.run(), so an application run
# as one task has nothing to carry it
function interthread_needs_tasks_error()
    ErrorException("Goby.INTERTHREAD is carried between task modules: run the application with " *
                   "Goby.run(app, Main, [TaskModule, ...]) and publish from start(), loop() or a " *
                   "callback once it is running")
end

function check_single_task()
    if !isempty(interthread_subscriptions()) || Goby.interthread_contract_declared()
        throw(interthread_needs_tasks_error())
    end
end

function run(goby_app, main_module, task_modules)
    tasks = Vector{Task}()
    Goby.is_multithreaded = true

    # 1. set cxx loop frequency, defaulting to 10 Hz (used for checking inbox messages)
    cxx_channel_check_frequency = 10
    if isdefined(Main, :goby_cfg) && haskey(Main.goby_cfg, :cxx_channel_check_frequency)
        cxx_channel_check_frequency = Main.goby_cfg[:cxx_channel_check_frequency]
    end
    println("Setting cxx channel check frequency to $cxx_channel_check_frequency")
    Goby.cxx_set_loop_frequency_hertz(goby_app, cxx_channel_check_frequency)

    # 2. create inboxes
    for task_module in task_modules
        task_id = TaskID(MultiThread.next_task_id.id)
        task_module_to_id[task_module] = task_id
        task_names[task_id] = nameof(task_module)
        task_inboxes[task_id] = TaskInbox(channel_size)
        MultiThread.next_task_id = TaskID(MultiThread.next_task_id.id + 1)
    end

    task_module_to_id[main_module] = MultiThread.main_task_id
    task_names[MultiThread.main_task_id] = nameof(main_module)
    MultiThread.cxx_task_id = TaskID(MultiThread.next_task_id.id)
    task_names[MultiThread.cxx_task_id] = Symbol("C++")
    task_inboxes[MultiThread.cxx_task_id] = TaskInbox(channel_size)

    min_nthreads = MultiThread.cxx_task_id.id
    if Threads.nthreads() < min_nthreads
        throw(AssertionError("Must run julia with at least $min_nthreads threads (use 'julia -t $min_nthreads')"))
    end

    # names in interface.yml are resolved once everything they name has been defined
    Goby.resolve_interthread_contract!()

    # 3. spawn tasks
    if isdefined(main_module, :start)
        main_module.start()
    end

    started = Threads.Atomic{Int}(0)
    for task_module in task_modules
        push!(tasks, task_spawn(task_module, started, length(task_modules)))

        if isdefined(task_module, :goby_cfg) && haskey(task_module.goby_cfg, :loop_function)
            push!(tasks, loop_timer(task_module))
        end
    end
    isempty(task_modules) && Goby.report_unmade_interthread_subscriptions()

    cxx_runner = (app) -> begin
        try
            Goby.cxx_run(app)
        catch e
            report_task_failure("the C++ application", e, catch_backtrace())
            rethrow()
        end
    end
    push!(tasks, ThreadPools.@tspawnat MultiThread.cxx_task_id.id cxx_runner(goby_app))
    println("Spawning main run() as task ID $MultiThread.cxx_task_id")

    if isdefined(main_module, :goby_cfg) && haskey(main_module.goby_cfg, :loop_function)
        push!(tasks, loop_timer(main_module))
    end

    # 4. Loop over all tasks checking messages
    while true
        task_channel_check(MultiThread.main_task_id)
        for task in tasks
            if istaskdone(task)
                fetch(task)
            end
        end
    end
end


# A task that throws takes its exception with it: run() is parked on Main's inbox and would
# wait there for a message the dead task will never send, so the application goes quiet with
# nothing said. Hand the exception to Main through that same inbox, which both wakes it and
# gives it something to report.
function report_task_failure(what::String, e, backtrace)
    post!(task_inboxes[MultiThread.main_task_id], (:task_failed, what, e, backtrace))
end

function task_spawn(task_module::Module, started::Threads.Atomic{Int}, ntasks::Int)
    task_id = task_module_to_id[task_module]
    println("Spawning $run as task ID $task_id")

    runner = (id, mod::Module) -> begin
        try
            if isdefined(mod, :start)
                mod.start()
            end
            # the last task to finish start() is the one that knows startup is over
            if Threads.atomic_add!(started, 1) + 1 == ntasks
                Goby.report_unmade_interthread_subscriptions()
            end
            while true
                task_channel_check(id)
            end
        catch e
            report_task_failure("task module $(mod)", e, catch_backtrace())
            rethrow()
        end
    end

    t = ThreadPools.@tspawnat task_id.id runner(task_id, task_module)
    println("Task: $t")
    return t
end

function cxx_loop()
    # Julia loop is called from timer trigger
    # but we use C++ loop() to let cxx_task check its inbox
    while is_ready(task_inboxes[MultiThread.cxx_task_id])
        task_channel_check(MultiThread.cxx_task_id)
    end
end


function loop_timer(task_module::Module)
    task_id = task_module_to_id[task_module]

    timer = (id, mod::Module) -> begin
        try
            while true
                post!(task_inboxes[id], (:loop, mod.goby_cfg[:loop_function]))
                sleep(1/mod.goby_cfg[:loop_frequency])
            end
        catch e
            report_task_failure("loop timer for $(mod)", e, catch_backtrace())
            rethrow()
        end
    end

    t = ThreadPools.@tspawnat MultiThread.timer_task_id.id timer(task_id, task_module)
    return t
end

function task_channel_check(task_id::TaskID)
    item = take_next!(task_inboxes[task_id])

    if item[1] == :message
        sub = item[2]
        sub.callback(item[3])
        return
    end

    packet = item[2]
    type = packet[1]
    if type == :loop
        loop_func = packet[2]
        loop_func()
    elseif type == :cxx_publish
        app = packet[2]
        layer = packet[3]
        group = packet[4]
        msg = packet[5]
        Goby.publish(app, layer, group, msg)
    elseif type == :cxx_subscribe
        # runs on the task that owns the application, whichever task asked for it
        app = packet[2]
        layer_int = packet[3]
        type_name = packet[4]
        scheme = packet[5]
        group = packet[6]
        Goby.cxx_subscribe(app, layer_int, type_name, scheme, group, "receive", "Goby")
    elseif type == :cxx_receive
        layer = packet[2]
        type_name = packet[3]
        scheme = packet[4]
        group = packet[5]
        bytes = packet[6]
        Goby.receive_dereferenced(layer, type_name, scheme, group, bytes)
    elseif type == :task_failed
        what = packet[2]
        e = packet[3]
        backtrace = packet[4]
        println(stderr, "Goby: $(what) stopped and the application cannot continue:")
        Base.showerror(stderr, e, backtrace)
        println(stderr)
        throw(e)
    end
end


end # module MultiThread
