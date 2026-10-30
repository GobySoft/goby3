# Checks the generate -> compile -> load chain for the Julia bindings.
#
# Loads the library built from interface.yml into the Goby module and checks that the C++ side
# registered what the Julia API calls into. Actually exchanging messages needs a portal and a
# running gobyd, and belongs in an integration test rather than here.
#
# Run as: julia test_app.jl <Goby.jl project directory>
# The library is found beside this script, which is the layout goby_add_julia_app() produces.

using Pkg
Pkg.activate(ARGS[1], io = devnull)

using Goby
using Test
using ThreadPools

const LIBRARY = joinpath(@__DIR__, "libgoby_test_julia_app.so")

@testset "generated Julia bindings" begin

    @test isfile(LIBRARY)

    # the launcher passes goby_add_julia_app()'s THREADS to julia, which a multi-threaded
    # application depends on: Julia cannot change its thread count once started
    @testset "launcher thread count" begin
        @test Threads.nthreads() >= 3
    end

    @eval Goby begin
        @wrapmodule(() -> $LIBRARY)
        function __init__()
            @initcxx
        end
    end

    # the application class named by 'application: name' in interface.yml
    @testset "application type" begin
        @test isdefined(Goby, :GobyJuliaTestApp)
        @test Goby.GobyJuliaTestApp isa Type
        # ApplicationWrapper is constructed from a TextFormat configuration string
        @test hasmethod(Goby.GobyJuliaTestApp, Tuple{String})
    end

    # the constants Goby.publish and Goby.subscribe pass across the language boundary
    @testset "layer and scheme constants" begin
        for layer in (:INTERTHREAD, :INTERPROCESS, :INTERMODULE)
            @test isdefined(Goby, layer)
        end
        for scheme in (:NULL_SCHEME, :PROTOBUF, :JSON)
            @test isdefined(Goby, scheme)
        end
        @test Int32(Goby.INTERPROCESS) != Int32(Goby.INTERTHREAD)
    end

    # the methods Goby.jl calls on the wrapper
    @testset "wrapper methods" begin
        for method in (:cxx_run, :cxx_publish, :cxx_subscribe,
                       :cxx_set_loop_frequency_hertz, :cxx_cfg_serialized)
            @test isdefined(Goby, method)
        end
    end

    # the Julia side generated from the same interface.yml
    include(joinpath(@__DIR__, "goby_test_julia_app_goby.jl"))

    @testset "generated module" begin
        @test GobyJuliaTestAppGoby.APPLICATION_NAME == "GobyJuliaTestApp"

        # the group constant is the interface.yml expression, which is what the C++ side matches
        # on -- not the group's runtime name, which need not be the same
        @test GobyJuliaTestAppGoby.groups.tx == "goby::test::julia::groups::tx"
        @test GobyJuliaTestAppGoby.groups.rx == "goby::test::julia::groups::rx"

        # one accessor per portal, named for the layer when no alias is given
        @test GobyJuliaTestAppGoby.interprocess() == Goby.INTERPROCESS
    end

    # Subscribing on a layer the C++ side owns is registered twice: once as a callback to run,
    # and once as the task to deliver to. Both are keyed by task, so two tasks subscribing to the
    # same thing get one each rather than the second replacing the first. Exercised directly
    # because actually moving a message needs a portal.
    @testset "per-task subscription registry" begin
        layer = Int32(Goby.INTERPROCESS)
        scheme = Int(Goby.PROTOBUF)
        type_name = "NavigationReport"
        group = GobyJuliaTestAppGoby.groups.rx

        main_callback = msg -> nothing
        task_callback = msg -> nothing

        Goby.register_callback(layer, scheme, type_name, group, main_callback)
        subscribing_task = ThreadPools.@tspawnat 2 Goby.register_callback(layer, scheme, type_name, group, task_callback)
        wait(subscribing_task)

        registered = Goby.callbacks_for(Int(layer), scheme, type_name, group)
        @test length(registered) == 2
        @test Set(owner for (owner, _) in registered) == Set([1, 2])

        # the same task subscribing again replaces its own callback rather than adding one
        replacement = msg -> nothing
        Goby.register_callback(layer, scheme, type_name, group, replacement)
        registered = Goby.callbacks_for(Int(layer), scheme, type_name, group)
        @test length(registered) == 2
        @test registered[findfirst(entry -> entry[1] == 1, registered)][2] === replacement

        @test isempty(Goby.callbacks_for(Int(layer), scheme, type_name, "never::subscribed"))
    end

    @testset "delivery routing" begin
        key = (Int32(Goby.INTERPROCESS), Int(Goby.PROTOBUF), "NavigationReport",
               GobyJuliaTestAppGoby.groups.rx)
        main_channel = Goby.MultiThread.task_interthread_channels[Goby.MultiThread.main_task_id]

        # nothing has registered for this one, so it goes to Main as it did before tasks could
        # subscribe for themselves
        @test Goby.MultiThread.cxx_subscriber_channels(key) == [main_channel]

        Goby.MultiThread.record_cxx_subscriber(key, Goby.MultiThread.main_task_id)
        @test Goby.MultiThread.cxx_subscriber_channels(key) == [main_channel]

        # recording the same task twice must not deliver the message to it twice
        Goby.MultiThread.record_cxx_subscriber(key, Goby.MultiThread.main_task_id)
        @test Goby.MultiThread.cxx_subscriber_channels(key) == [main_channel]
    end
end
