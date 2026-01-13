module MultiThread

using ..Goby
using ThreadPools

########################
# Interthread          #
# Using Tasks/Channels #
########################

# map of module to task_id
task_module_to_id=Dict{Module, Goby.TaskID}()
# map of task_id to vector of channels (used for 'check')
task_interthread_channels=Dict{Goby.TaskID, Channel}()
channel_size = 50
task_interthread_channels[Goby.main_task_id] = Channel(channel_size)

# map of group to vector of channels (used for 'publish')
group_interthread_channels=Dict{String, Vector{Channel}}()
group_interthread_channels_lock = Threads.ReentrantLock()

function publish_interthread(task_id::Goby.TaskID, group, msg)
    Threads.lock(group_interthread_channels_lock) do
        if haskey(group_interthread_channels, group)
            for channel in group_interthread_channels[group]
                put!(channel, (:interthread_receive, group, msg))
            end
        end
    end
end

function publish_forward_interprocess(app, layer, group, msg)
    put!(task_interthread_channels[Goby.cxx_task_id], (:cxx_publish, app, layer, group, msg))
end
        
function receive_forward_interprocess(layer, type_name, scheme, group, bytes)
    put!(task_interthread_channels[Goby.main_task_id], (:cxx_receive, layer, type_name, scheme, group, bytes))
end


function subscribe_interthread(task_id::Goby.TaskID, group, callback::Function)
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
        task_id = Goby.TaskID(Goby.next_task_id.id)
        task_module_to_id[task_module] = task_id
        task_interthread_channels[task_id] = Channel(channel_size)
        Goby.next_task_id = Goby.TaskID(Goby.next_task_id.id + 1)
    end
    
    task_module_to_id[main_module] = Goby.main_task_id
    Goby.cxx_task_id = Goby.TaskID(Goby.next_task_id.id)        
    task_interthread_channels[Goby.cxx_task_id] = Channel(channel_size)
    
    
    # 3. spawn tasks
    for task_module in task_modules
        push!(tasks, task_spawn(task_module))
        
        if isdefined(task_module, :goby_cfg) && haskey(task_module.goby_cfg, :loop_function)
            push!(tasks, loop_timer(task_module))
        end
    end
    
    push!(tasks, ThreadPools.@tspawnat Goby.cxx_task_id.id Goby.cxx_run(goby_app))
    println("Spawning main run() as task ID $Goby.cxx_task_id")
   
    if isdefined(main_module, :goby_cfg) && haskey(main_module.goby_cfg, :loop_function)
        push!(tasks, loop_timer(main_module))
    end
    
    # 4. Loop over all tasks checking messages
    while true
        task_channel_check(Goby.main_task_id)
        for task in tasks
            if istaskdone(task)
                fetch(task)
            end
        end
    end
end


function task_spawn(task_module::Module)
    task_id = task_module_to_id[task_module]
    println("Spawning $run as task ID $task_id")

    runner = (id, mod::Module) -> begin
        mod.run(id)
        while true
            task_channel_check(id)
        end
    end
    
    t = ThreadPools.@tspawnat task_id.id runner(task_id, task_module)
    println("Task: $t")
    return t
end

function cxx_loop()
    # Julia loop is called from timer trigger
    # but we use C++ loop() to let cxx_task check its channel
    while isready(task_interthread_channels[Goby.cxx_task_id])
        task_channel_check(Goby.cxx_task_id)
    end
end


function loop_timer(task_module::Module)
    task_id = task_module_to_id[task_module]

    timer = (id, mod::Module) -> begin
        while true
            put!(task_interthread_channels[id], (:loop, mod.goby_cfg[:loop_function]))
            sleep(1/mod.goby_cfg[:loop_frequency])
#            println("Loop timer: $task_id")
        end
    end

    t = ThreadPools.@tspawnat Goby.timer_task_id.id timer(task_id, task_module)
    return t
end    

function task_channel_check(task_id::Goby.TaskID)
    packet = take!(task_interthread_channels[task_id])

#    println("Task $task_id received packet $packet")
    type = packet[1]
    if type == :interthread_receive
        task_callbacks = task_local_storage(:callbacks)
        group = packet[2]
        msg = packet[3]
        if haskey(task_callbacks, group)
            callbacks = task_callbacks[group]
            for callback in callbacks
                callback(task_id, msg)
            end
        end
    elseif type == :loop
        loop_func = packet[2]
        loop_func(task_id)
    elseif type == :cxx_publish
        app = packet[2]
        layer = packet[3]
        group = packet[4]
        msg = packet[5]
        publish(app, layer, group, msg)
    elseif type == :cxx_receive
        layer = packet[2]
        type_name = packet[3]
        scheme = packet[4]
        group = packet[5]
        bytes = packet[6]
        receive_dereferenced(layer, type_name, scheme, group, bytes)
    end    
end


end # module MultiThread
