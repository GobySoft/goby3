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

"""The compiled extension this process is using.

The ``goby`` package is pure Python and architecture-independent; the compiled code lives in the
extension module generated for each application. ``goby.time`` and ``goby.glog`` need that
extension, so the generated module hands it over as it is imported, before any application class
exists.
"""

import threading

_lock = threading.Lock()
_extension = None
_observers = []


def bind(extension):
    """Records the extension module this process is running against.

    Called by the module generated from ``interface.yml``. An application imports exactly one, so
    a second call with a different extension is a mistake worth reporting rather than a state to
    support.
    """
    global _extension

    with _lock:
        if _extension is extension:
            return
        if _extension is not None:
            raise RuntimeError(
                "Two Goby extension modules were imported into one process "
                f"({_extension.__name__} and {extension.__name__}). A Goby application is built "
                "from one interface.yml and imports one generated module."
            )
        _extension = extension
        observers = list(_observers)

    for observer in observers:
        observer(extension)


def extension():
    """The bound extension module, or None when nothing has imported a generated module yet."""
    return _extension


def require(what):
    """Returns the bound extension, or explains what the caller needed it for."""
    if _extension is None:
        raise RuntimeError(
            f"{what} needs the extension module generated from your interface.yml. Import your "
            "application's generated module first; goby.run() does this for you."
        )
    return _extension


def on_bind(observer):
    """Calls ``observer(extension)`` when the extension is bound, or now if it already is."""
    with _lock:
        if _extension is None:
            _observers.append(observer)
            return
        extension_ = _extension

    observer(extension_)
