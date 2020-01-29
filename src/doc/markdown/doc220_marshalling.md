# goby-middleware: Marshalling schemes

Generally speaking, Goby does not require the use of a particular marshalling scheme (for converting data to bytes and vice-versa). The interthread layer performs no marshalling at all, so this discussion is not relevant for that layer (goby::middleware::MarshallingScheme::CXX_OBJECT is the scheme identifier for this layer). The intervehicle layer requires the use of the DCCL marshalling scheme (<http://libdccl.org>).

Thus, this discussion is most relevant for the interprocess layer, where any number of schemes might be employed (Google Protocol Buffers, MAVLink, JSON, msgpack, etc.). Basically, any code that can reversibly transform a C++ object into a set of bytes is suitable for using as a Goby scheme.

Throughout the documentation and code, "marshalling scheme" is shortened to just *scheme*. The schemes supported directly in Goby are enumerated in goby::middleware::MarshallingScheme::MarshallingSchemeEnum, however the actual value passed around is an integer, allowing the user to define their own schemes without modifying the Goby source code. In this case, scheme identifiers must be chosen that do not conflict with any of the goby::middleware::MarshallingScheme::MarshallingSchemeEnum values or any other third-party schemes in use.

In practice, a real system should seek to minimize the number of schemes in use. However, it is often very helpful for interoperability to avoid premature data translation which may requires several different schemes depending on the complexity of the system, and how much of the code involves interacting with pre-existing modules (that may be fully committed to one particular scheme).

## Types within a scheme

The *type* name must be unique only within a scheme as the (*scheme*, *type*, *group*) tuple is used to distinguish different publications.

Thus, you could have a MAVLink message called "foo" and a Protobuf message also called "foo", and these would not conflict (as the *scheme* id is different). However, if you have two Protobuf messages both called "foo" this would create a conflict (within Goby, but also probably within libprotobuf).

## Implementing a new scheme

It is relatively straightforward to implement a new scheme in Goby, presuming you have an existing library that can marshal (serialize) and unmarshal (parse) the data.

To implement your new scheme, you need to

* Add a new enumeration to goby::middleware::MarshallingScheme::MarshallingSchemeEnum (if modifying Goby directly, otherwise allocate a new integer in your project's internal scheme registry).
* Implement (specialize) goby::middleware::SerializerParserHelper. The goby::middleware::SerializerParserHelper::parse_dynamic() and goby::middleware::SerializerParserHelper::type_name(const DataType& d) methods are optional, but may be helpful if your scheme has a concept of runtime introspection, which allows use of the goby::middleware::InterProcessTransporterBase::subscribe_regex and goby::middleware::InterProcessTransporterBase::subscribe_type_regex methods.
* (optional but higly recommended) Implement (specialize) goby::middleware::scheme(). This will allow publish and subscribe calls to infer the marshalling scheme from the data type, rather than having to be explicitly passed with every call.

The C-string implementation in `goby/middleware/marshalling/cstr.h` provides a good minimal example (but does not implement the optional dynamic introspection methods - for these see the Protobuf or MAVLink implementations).
