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

"""Layer and marshalling scheme constants.

These mirror C++ enumerations, and are defined here in Python rather than re-exported from a
compiled module so that the ``goby`` package stays architecture-independent. The values are
checked against the C++ headers by ``tests/test_schemes.py``.
"""

import enum


class PubSubLayer(enum.IntEnum):
    """Mirrors ``goby::middleware::languages::PubSubLayer``."""

    INTERTHREAD = 0
    INTERPROCESS = 1
    INTERMODULE = 2


class MarshallingScheme(enum.IntEnum):
    """Mirrors ``goby::middleware::MarshallingScheme::MarshallingSchemeEnum``."""

    ALL_SCHEMES = -2
    NULL_SCHEME = -1
    CSTR = 0
    PROTOBUF = 1
    DCCL = 2
    CXX_OBJECT = 5
    MAVLINK = 6
    JSON = 7


INTERTHREAD = PubSubLayer.INTERTHREAD
INTERPROCESS = PubSubLayer.INTERPROCESS
INTERMODULE = PubSubLayer.INTERMODULE

ALL_SCHEMES = MarshallingScheme.ALL_SCHEMES
NULL_SCHEME = MarshallingScheme.NULL_SCHEME
CSTR = MarshallingScheme.CSTR
PROTOBUF = MarshallingScheme.PROTOBUF
DCCL = MarshallingScheme.DCCL
CXX_OBJECT = MarshallingScheme.CXX_OBJECT
MAVLINK = MarshallingScheme.MAVLINK
JSON = MarshallingScheme.JSON
