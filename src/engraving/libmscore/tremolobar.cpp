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

#include "tremolobar.h"
#include "draw/pen.h"
#include "rw/xml.h"
#include "types/typesconv.h"

#include "score.h"
#include "undo.h"
#include "staff.h"
#include "chord.h"
#include "note.h"

using namespace mu;
using namespace mu::engraving;

namespace mu::engraving {
//---------------------------------------------------------
//   tremoloBarStyle
//---------------------------------------------------------

static const ElementStyle tremoloBarStyle {
    { Sid::tremoloBarLineWidth,  Pid::LINE_WIDTH },
};

static const PitchValues DIP_CURVE = { PitchValue(0, 0),
                                       PitchValue(30, -100),
                                       PitchValue(60, 0) };

static const PitchValues DIVE_CURVE = { PitchValue(0, 0),
                                        PitchValue(60, -150) };

static const PitchValues RELEASE_UP_CURVE = { PitchValue(0, -150),
                                              PitchValue(60, 0) };

static const PitchValues INVERTED_DIP_CURVE = { PitchValue(0, 0),
                                                PitchValue(30, 100),
                                                PitchValue(60, 0) };

static const PitchValues RETURN_CURVE = { PitchValue(0, 0),
                                          PitchValue(60, 150) };

static const PitchValues RELEASE_DOWN_CURVE = { PitchValue(0, 150),
                                                PitchValue(60, 0) };

//---------------------------------------------------------
//   TremoloBar
//---------------------------------------------------------

TremoloBar::TremoloBar(EngravingItem* parent)
    : EngravingItem(ElementType::TREMOLOBAR, parent, ElementFlag::MOVABLE | ElementFlag::ON_STAFF)
{
    initElementStyle(&tremoloBarStyle);
}

//---------------------------------------------------------
//   layout
//---------------------------------------------------------

void TremoloBar::layout()
{
    double _spatium = spatium();
    if (explicitParent()) {
        setPos(0.0, -_spatium * 3.0);
    } else {
        setPos(PointF());
    }

    /* we place the tremolo bars starting slightly before the
     *  notehead, and end it slightly after, drawing above the
     *  note. The values specified in Guitar Pro are very large, too
     *  large for the scale used in Musescore. We used the
     *  timeFactor and pitchFactor below to reduce these values down
     *  consistently to values that make sense to draw with the
     *  Musescore scale. */

    double timeFactor  = m_userMag / 1.0;
    double pitchFactor = -_spatium * .02;

    m_polygon.clear();
    for (auto v : m_points) {
        m_polygon << PointF(v.time * timeFactor, v.pitch * pitchFactor);
    }

    double w = m_lw.val();
    setbbox(m_polygon.boundingRect().adjusted(-w, -w, w, w));
}

//---------------------------------------------------------
//   draw
//---------------------------------------------------------

void TremoloBar::draw(mu::draw::Painter* painter) const
{
    TRACE_OBJ_DRAW;
    using namespace mu::draw;
    Pen pen(curColor(), m_lw.val(), PenStyle::SolidLine, PenCapStyle::RoundCap, PenJoinStyle::RoundJoin);
    painter->setPen(pen);
    painter->drawPolyline(m_polygon);
}

//---------------------------------------------------------
//   write
//---------------------------------------------------------

void TremoloBar::write(XmlWriter& xml) const
{
    xml.startElement(this);
    writeProperty(xml, Pid::MAG);
    writeProperty(xml, Pid::LINE_WIDTH);
    writeProperty(xml, Pid::PLAY);
    for (const PitchValue& v : m_points) {
        xml.tag("point", { { "time", v.time }, { "pitch", v.pitch }, { "vibrato", v.vibrato } });
    }
    xml.endElement();
}

//---------------------------------------------------------
//   read
//---------------------------------------------------------

void TremoloBar::read(XmlReader& e)
{
    while (e.readNextStartElement()) {
        auto tag = e.name();
        if (tag == "point") {
            PitchValue pv;
            pv.time    = e.intAttribute("time");
            pv.pitch   = e.intAttribute("pitch");
            pv.vibrato = e.intAttribute("vibrato");
            m_points.push_back(pv);
            e.readNext();
        } else if (tag == "mag") {
            m_userMag = e.readDouble(0.1, 10.0);
        } else if (readStyledProperty(e, tag)) {
        } else if (tag == "play") {
            setPlay(e.readInt());
        } else if (readProperty(tag, e, Pid::LINE_WIDTH)) {
        } else {
            e.unknown();
        }
    }
}

//---------------------------------------------------------
//   getProperty
//---------------------------------------------------------

PropertyValue TremoloBar::getProperty(Pid propertyId) const
{
    switch (propertyId) {
    case Pid::LINE_WIDTH:
        return lineWidth().val();
    case Pid::MAG:
        return userMag();
    case Pid::PLAY:
        return play();
    case Pid::TREMOLOBAR_TYPE:
        return static_cast<int>(parseTremoloBarTypeFromCurve(m_points));
    case Pid::TREMOLOBAR_CURVE:
        return m_points;
    default:
        return EngravingItem::getProperty(propertyId);
    }
}

//---------------------------------------------------------
//   setProperty
//---------------------------------------------------------

bool TremoloBar::setProperty(Pid propertyId, const PropertyValue& v)
{
    switch (propertyId) {
    case Pid::LINE_WIDTH:
        setLineWidth(Spatium(v.value<double>()));
        break;
    case Pid::MAG:
        setUserMag(v.toDouble());
        break;
    case Pid::PLAY:
        setPlay(v.toBool());
        score()->setPlaylistDirty();
        break;
    case Pid::TREMOLOBAR_TYPE:
        updatePointsByTremoloBarType(static_cast<TremoloBarType>(v.toInt()));
        break;
    case Pid::TREMOLOBAR_CURVE:
        setPoints(v.value<PitchValues>());
        break;
    default:
        return EngravingItem::setProperty(propertyId, v);
    }
    triggerLayout();
    return true;
}

//---------------------------------------------------------
//   propertyDefault
//---------------------------------------------------------

PropertyValue TremoloBar::propertyDefault(Pid pid) const
{
    switch (pid) {
    case Pid::MAG:
        return 1.0;
    case Pid::PLAY:
        return true;
    case Pid::TREMOLOBAR_TYPE:
        return static_cast<int>(TremoloBarType::DIP);
    case Pid::TREMOLOBAR_CURVE:
        return DIP_CURVE;
    default:
        for (const StyledProperty& p : *styledProperties()) {
            if (p.pid == pid) {
                if (propertyType(pid) == P_TYPE::MILLIMETRE) {
                    return score()->styleMM(p.sid);
                }
                return score()->styleV(p.sid);
            }
        }
        return EngravingItem::propertyDefault(pid);
    }
}

TremoloBarType TremoloBar::parseTremoloBarTypeFromCurve(const PitchValues& curve) const
{
    if (curve == DIP_CURVE) {
        return TremoloBarType::DIP;
    } else if (curve == DIVE_CURVE) {
        return TremoloBarType::DIVE;
    } else if (curve == RELEASE_UP_CURVE) {
        return TremoloBarType::RELEASE_UP;
    } else if (curve == INVERTED_DIP_CURVE) {
        return TremoloBarType::INVERTED_DIP;
    } else if (curve == RETURN_CURVE) {
        return TremoloBarType::RETURN;
    } else if (curve == RELEASE_DOWN_CURVE) {
        return TremoloBarType::RELEASE_DOWN;
    }

    return TremoloBarType::CUSTOM;
}

void TremoloBar::updatePointsByTremoloBarType(const TremoloBarType type)
{
    switch (type) {
    case TremoloBarType::DIP:
        m_points = DIP_CURVE;
        break;
    case TremoloBarType::DIVE:
        m_points = DIVE_CURVE;
        break;
    case TremoloBarType::RELEASE_UP:
        m_points = RELEASE_UP_CURVE;
        break;
    case TremoloBarType::INVERTED_DIP:
        m_points = INVERTED_DIP_CURVE;
        break;
    case TremoloBarType::RETURN:
        m_points = RETURN_CURVE;
        break;
    case TremoloBarType::RELEASE_DOWN:
        m_points = RELEASE_DOWN_CURVE;
        break;
    case TremoloBarType::CUSTOM:
        break;
    }
}
}
