module MultiThread

using ..Goby
using ThreadPools

########################
# Interthread          #
# Using Tasks/Channels #
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


# map of module to task_id
task_module_to_id=Dict{Module, TaskID}()
# map of task_id to vector of channels (used for 'check')
task_interthread_channels=Dict{TaskID, Channel}()
channel_size = 50
task_interthread_channels[MultiThread.main_task_id] = Channel(channel_size)

# map of group to vector of channels (used for 'publish')
group_interthread_channels=Dict{String, Vector{Channel}}()
group_interthread_channels_lock = Threads.ReentrantLock()

# Check for and intercept interthread publications
# Return true if we did, false if this isn't our publication
function check_and_publish(app, layer, group, msg)
    layer_int::Int32 = Int32(layer)
    # named up front so that the branches reporting it have it too
    task_id = MultiThread.TaskID(Threads.threadid())
    if layer_int == Int32(Goby.INTERTHREAD)
        publish_interthread(task_id, group, msg)
        return true
    elseif Threads.threadid() == MultiThread.cxx_task_id.id
        # Pass back to normal publish
        return false
    elseif Threads.threadid() != MultiThread.main_task_id.id
        throw(AssertionError("publish for layer $layer is not yet supported on non-Main threads (from $task_id)"))
    else
        publish_forward_interprocess(app, layer, group, msg)
        return true
    end    

    # Pass back to normal publish
    return false
end

function check_and_subscribe(layer, group, callback::Function)
    layer_int::Int32 = Int32(layer)
    if layer_int == Int32(Goby.INTERTHREAD)        
        task_id = MultiThread.TaskID(Threads.threadid())
        MultiThread.subscribe_interthread(task_id, group, callback)
        return true
    elseif Threads.threadid() != MultiThread.main_task_id.id
        throw(AssertionError("subscribe for layer $layer is not yet supported on non-Main threads"))     
    end

    # Pass back to normal subscribe
    return false
end

function check_and_receive(layer, type_name, scheme, group, bytes)
    if Threads.threadid() == MultiThread.cxx_task_id.id
        receive_forward_interprocess(layer, type_name, scheme, group, bytes)
        return true
    end
    return false
end

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
    put!(task_interthread_channels[MultiThread.cxx_task_id], (:cxx_publish, app, layer, group, msg))
end
        
function receive_forward_interprocess(layer, type_name, scheme, group, bytes)
    # TODO - support non-main thread subscribe for non-interthread messages
    put!(task_interthread_channels[MultiThread.main_task_id], (:cxx_receive, layer, type_name, scheme, group, bytes))
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

    # 1. set cxx loop frequency, defaulting to 10 Hz (used for checking channel messages)
    cxx_channel_check_frequency = 10
    if isdefined(Main, :goby_cfg) && haskey(Main.goby_cfg, :cxx_channel_check_frequency)
        cxx_channel_check_frequency = Main.goby_cfg[:cxx_channel_check_frequency]
    end
    println("Setting cxx channel check frequency to $cxx_channel_check_frequency")
    Goby.cxx_set_loop_frequency_hertz(goby_app, cxx_channel_check_frequency)
    
    # 2. create channels
    for task_module in task_modules
        task_id = TaskID(MultiThread.next_task_id.id)
        task_module_to_id[task_module] = task_id
        task_interthread_channels[task_id] = Channel(channel_size)
        MultiThread.next_task_id = TaskID(MultiThread.next_task_id.id + 1)
    end
    
    task_module_to_id[main_module] = MultiThread.main_task_id
    MultiThread.cxx_task_id = TaskID(MultiThread.next_task_id.id)        
    task_interthread_channels[MultiThread.cxx_task_id] = Channel(channel_size)

    min_nthreads = MultiThread.cxx_task_id.id
    if Threads.nthreads() < min_nthreads
        throw(AssertionError("Must run julia with at least $min_nthreads threads (use 'julia -t $min_nthreads')"))
    end
    
    # 3. spawn tasks
    if isdefined(main_module, :start)
        main_module.start()
    end
    
    for task_module in task_modules
        push!(tasks, task_spawn(task_module))
        
        if isdefined(task_module, :goby_cfg) && haskey(task_module.goby_cfg, :loop_function)
            push!(tasks, loop_timer(task_module))
        end
    end
    
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


# A task that throws takes its exception with it: run() is parked on Main's channel and would
# wait there for a message the dead task will never send, so the application goes quiet with
# nothing said. Hand the exception to Main through that same channel, which both wakes it and
# gives it something to report.
function report_task_failure(what::String, e, backtrace)
    put!(task_interthread_channels[MultiThread.main_task_id], (:task_failed, what, e, backtrace))
end

function task_spawn(task_module::Module)
    task_id = task_module_to_id[task_module]
    println("Spawning $run as task ID $task_id")

    runner = (id, mod::Module) -> begin
        try
            if isdefined(mod, :start)
                mod.start()
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
    # but we use C++ loop() to let cxx_task check its channel
    while isready(task_interthread_channels[MultiThread.cxx_task_id])
        task_channel_check(MultiThread.cxx_task_id)
    end
end


function loop_timer(task_module::Module)
    task_id = task_module_to_id[task_module]

    timer = (id, mod::Module) -> begin
        try
            while true
                put!(task_interthread_channels[id], (:loop, mod.goby_cfg[:loop_function]))
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
    packet = take!(task_interthread_channels[task_id])

    type = packet[1]
    if type == :interthread_receive
        task_callbacks = task_local_storage(:callbacks)
        group = packet[2]
        msg = packet[3]
        if haskey(task_callbacks, group)
            callbacks = task_callbacks[group]
            for callback in callbacks
                callback(msg)
            end
        end
    elseif type == :loop
        loop_func = packet[2]
        loop_func()
    elseif type == :cxx_publish
        app = packet[2]
        layer = packet[3]
        group = packet[4]
        msg = packet[5]
        Goby.publish(app, layer, group, msg)
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
