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

const LIBRARY = joinpath(@__DIR__, "libgoby_test_julia_app.so")

@testset "generated Julia bindings" begin

    @test isfile(LIBRARY)

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
end
