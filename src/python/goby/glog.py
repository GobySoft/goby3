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

"""The Goby logger, ``goby::glog``, from Python.

A Goby application is configured with ``app { glog_config { tty_verbosity: ... } }``, and every
C++ application's output goes to that one place at that one verbosity. These functions put a
Python application's output there too::

    import goby

    goby.glog.verbose("connected to the device")
    goby.glog.warn(f"no reply in {timeout}s")

For an application that already uses the standard ``logging`` module, ``install()`` routes it
here instead, so nothing else has to change::

    import logging

    goby.glog.install()
    logging.getLogger(__name__).info("connected to the device")

Levels map onto Goby's verbosities: ``DEBUG`` and below to ``DEBUG1``-``DEBUG3``, ``INFO`` to
``VERBOSE``, and ``WARNING`` and above to ``WARN``. Nothing maps to ``DIE``, which terminates the
application rather than describing a record; call ``die()`` deliberately for that.

The logger is configured while the application is constructed, so writes from before then are
dropped. That covers module import and anything run before ``goby.run()``.
"""

import logging

from . import _runtime

__all__ = [
    "DEBUG1",
    "DEBUG2",
    "DEBUG3",
    "DIE",
    "GlogHandler",
    "QUIET",
    "VERBOSE",
    "WARN",
    "add_group",
    "debug1",
    "debug2",
    "debug3",
    "die",
    "install",
    "is_enabled",
    "verbose",
    "warn",
    "write",
]

# goby::util::logger::Verbosity
DIE = -1
QUIET = 0
WARN = 1
VERBOSE = 2
DEBUG1 = 4
DEBUG2 = 5
DEBUG3 = 6

_LOGGING_TO_VERBOSITY = (
    (logging.WARNING, WARN),
    (logging.INFO, VERBOSE),
    (logging.DEBUG, DEBUG1),
)


def verbosity_for(level):
    """The Goby verbosity a ``logging`` level maps onto."""
    for threshold, verbosity in _LOGGING_TO_VERBOSITY:
        if level >= threshold:
            return verbosity
    # a level below DEBUG is asking for more detail, which is what DEBUG2 and DEBUG3 are for
    return DEBUG2 if level >= logging.DEBUG // 2 else DEBUG3


def write(verbosity, text, group=""):
    """Writes one line at ``verbosity``, optionally into a named group."""
    extension = _runtime.extension()
    if extension is None:
        return
    extension._glog_write(int(verbosity), str(group), str(text))


def is_enabled(verbosity):
    """Whether anything is listening at ``verbosity``.

    Worth checking before building an expensive message, as ``goby::glog.is()`` is in C++.
    """
    extension = _runtime.extension()
    if extension is None:
        return False
    return extension._glog_is(int(verbosity))


def add_group(name, description=""):
    """Declares a log group, as ``goby::glog.add_group()`` does.

    A group must be declared before it is written to.
    """
    extension = _runtime.require("goby.glog.add_group()")
    extension._glog_add_group(str(name), str(description))


def warn(text, group=""):
    """Writes a warning."""
    write(WARN, text, group)


def verbose(text, group=""):
    """Writes at the default verbosity."""
    write(VERBOSE, text, group)


def debug1(text, group=""):
    write(DEBUG1, text, group)


def debug2(text, group=""):
    write(DEBUG2, text, group)


def debug3(text, group=""):
    write(DEBUG3, text, group)


def die(text, group=""):
    """Writes the message and terminates the application, as ``goby::glog`` does at DIE."""
    extension = _runtime.require("goby.glog.die()")
    extension._glog_write(int(DIE), str(group), str(text))


class GlogHandler(logging.Handler):
    """A ``logging`` handler that writes to ``goby::glog``.

    :param group: log group to write into; it must have been declared with ``add_group()``
    """

    def __init__(self, group=""):
        super().__init__()
        self.group = group

    def emit(self, record):
        try:
            write(verbosity_for(record.levelno), self.format(record), self.group)
        except Exception:  # noqa: BLE001 - a logging handler must not raise into its caller
            self.handleError(record)


def install(logger=None, level=logging.DEBUG, group="", replace=True):
    """Routes the standard ``logging`` module to the Goby logger.

    :param logger: the logger to attach to; the root logger by default
    :param level: the level below which records are dropped before reaching Goby. The default
        passes everything and lets ``glog_config`` decide, which is the point of the bridge.
    :param group: log group to write into
    :param replace: remove any handlers already attached, so records are not also printed
        straight to the terminal outside Goby's formatting
    :return: the installed handler
    """
    logger = logging.getLogger() if logger is None else logger

    if replace:
        for existing in list(logger.handlers):
            logger.removeHandler(existing)

    handler = GlogHandler(group)
    handler.setLevel(level)
    logger.addHandler(handler)
    logger.setLevel(min(logger.level, level) if logger.level else level)
    return handler
