# Copyright 2026:
#   GobySoft, LLC (2013-)
#   Community contributors (see AUTHORS file)
# File authors:
#   Toby Schneider <toby@gobysoft.org>
#
#
# This file is part of the Goby Underwater Autonomy Project Libraries
# ("The Goby Libraries").
#
# The Goby Libraries are free software: you can redistribute them and/or modify
# them under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 2.1 of the License, or
# (at your option) any later version.
#
# The Goby Libraries are distributed in the hope that they will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with Goby.  If not, see <http://www.gnu.org/licenses/>.

"""Simulation-aware time, mirroring ``goby::time``.

Under ``app { simulation { time { use_sim_time: true warp_factor: 10 } } }`` the C++ clocks run
ten times faster than the wall clock, and everything Goby schedules -- loop rates, timeouts --
is expressed in that simulated time. An application that reads ``time.monotonic()`` or calls
``time.sleep()`` directly is on the wall clock, so at warp 10 it runs ten times slower than the
rest of the system.

Use these in place of the standard library's::

    import goby

    goby.time.sleep(0.5)           # half a simulated second
    stamp = goby.time.now()        # seconds since the epoch, on the simulated clock

``goby.run()`` and ``loop_frequency_hertz`` are already on this clock; this module is for an
application that does its own waiting or timestamping.

Outside simulation every function here is the standard library function, so code written against
it needs no branch.
"""

import time as _time

from . import _runtime

__all__ = [
    "monotonic",
    "now",
    "sleep",
    "using_sim_time",
    "warp_factor",
]

# (using_sim_time, warp_factor, reference_microtime), read from the extension on first use.
# Goby reads it out of the configuration, so it is fixed by the time an application runs.
_settings = None


def _read_settings():
    global _settings

    if _settings is None:
        extension = _runtime.extension()
        if extension is None:
            # nothing has been configured, so nothing is warped
            return (False, 1, 0)
        _settings = tuple(extension._sim_time())
    return _settings


def _reset_cache():
    """Forgets the cached settings; for tests, which configure more than once per process."""
    global _settings
    _settings = None


def using_sim_time():
    """Whether this application was configured to run on a simulated clock."""
    return bool(_read_settings()[0])


def warp_factor():
    """How much faster than the wall clock the simulated clock runs; 1 outside simulation."""
    using, warp, _ = _read_settings()
    return warp if using and warp > 0 else 1


def now():
    """Seconds since the UNIX epoch on the simulated clock, mirroring ``SystemClock::now()``.

    Outside simulation this is ``time.time()``.
    """
    using, warp, reference_microtime = _read_settings()
    real = _time.time()
    if not using:
        return real

    # t_sim = (t - t0) * w + t0, as SystemClock::warp() computes it
    reference = reference_microtime / 1e6
    return (real - reference) * warp + reference


def monotonic():
    """A monotonic clock in simulated seconds, mirroring ``SteadyClock::now()``.

    Use it for measuring simulated durations; unlike ``now()`` it is unaffected by changes to the
    system clock. Outside simulation this is ``time.monotonic()``.
    """
    return _time.monotonic() * warp_factor()


def sleep(seconds):
    """Sleeps for ``seconds`` of simulated time.

    At warp 10, ``sleep(1)`` returns after a tenth of a wall-clock second, so a driver polling its
    device keeps pace with the simulation rather than falling behind it.
    """
    if seconds <= 0:
        return
    _time.sleep(seconds / warp_factor())
