# Examples

The Goby3 examples reside in a separate repository:

https://github.com/GobySoft/goby3-examples

The examples are kept separate so that you can examine them without have to build Goby3 itself from source. The CMakeLists.txt and other build code here may also be helpful for you when building your own project repositories.

The structure of the repository is:

  - launch: `goby_launch` scripts for starting various examples
  - src
      - components: examples for various non-middleware components (acomms, util, moos).
      - interthread: examples that use the Goby middleware interthread layer.
      - interprocess: examples that showcase the Goby middleware interprocess layer.
      - intervehicle: examples using the Goby middleware intervehicle layer.
      - messages: Message definitions (Protobuf, etc.) used by the various middleware examples. Also goby::middleware::Group definitions are kept here.

See the goby3-examples [README](https://github.com/GobySoft/goby3-examples/blob/master/README.md) for more information.