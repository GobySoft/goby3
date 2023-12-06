# goby-zeromq: ZeroMQ Applications

Goby comes with a number of useful applications for use with the ZeroMQ implementation of the Goby3 middleware interprocess layer. These applications are summarized here, and those that require more detail have dedicated documentation pages.

## gobyd

gobyd provides two functions:

1. It runs a goby::zeromq::Router and goby::zeromq::Manager for mediating the ZeroMQ-based interprocess comms between all other apps (broker).
2. It can optionally run an intervehicle portal for connecting to various intervehicle modems and links to provide intervehicle layer communications for a particular node. Alternatively, this functionality can be run as a separate app using `goby_intervehicle_portal`.

## goby_intervehicle_portal

This application provides identical functionality to `gobyd`'s intervehicle portal. This is provided as a separate app for users who wish to keep the intervehicle comms (and associated drivers) separate from the ZeroMQ broker responsibilities for `gobyd`. This is especially helpful if the various drivers are less stable than the rest of the codebase, since if `gobyd` crashes (e.g., due to a faulty driver) it stops all interprocess comms.

## goby_gps

`goby_gps` is a client for [gpsd](https://gpsd.gitlab.io/gpsd/index.html) that publishes the GPS data from gpsd on several ZeroMQ interprocess groups (thus allowing Goby subscribers to access GPS data readily). `goby_gps` does not directly connect to the NMEA-0183 stream from the GPS device, as by using `gpsd` you open up the use of other useful clients, especially time-keeping (e.g., NTP).

See the [goby_gps](doc502_goby_gps.md) page for more details.

## goby_coroner

TODO: Document

## goby_frontseat_interface

TODO: Document

## goby_geov_interface

TODO: Document

## goby_opencpn_interface

TODO: Document

## goby_liaison

TODO: Document

## goby_logger

TODO: Document

## goby_terminate

TODO: Document
