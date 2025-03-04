/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef MU_ENGRAVING_AUDIO_H
#define MU_ENGRAVING_AUDIO_H

#include "types/bytearray.h"
#include "types/string.h"

namespace mu::engraving {
class XmlWriter;
class XmlReader;

//---------------------------------------------------------
//   Audio
//---------------------------------------------------------

class Audio
{
    String _path;
    ByteArray _data;

public:
    Audio();
    const String& path() const { return _path; }
    void setPath(const String& s) { _path = s; }
    const ByteArray& data() const { return _data; }
    ByteArray data() { return _data; }
    void setData(const ByteArray& ba) { _data = ba; }

    void read(XmlReader&);
    void write(XmlWriter&) const;
};
} // namespace mu::engraving
#endif // MU_ENGRAVING_AUDIO_H
