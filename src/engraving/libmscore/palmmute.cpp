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

#include "palmmute.h"
#include "rw/xml.h"
#include "system.h"
#include "measure.h"
#include "chordrest.h"
#include "score.h"
#include "stafftype.h"

using namespace mu;
using namespace mu::engraving;

namespace mu::engraving {
static const ElementStyle palmMuteStyle {
    { Sid::palmMuteFontFace,                      Pid::BEGIN_FONT_FACE },
    { Sid::palmMuteFontFace,                      Pid::CONTINUE_FONT_FACE },
    { Sid::palmMuteFontFace,                      Pid::END_FONT_FACE },
    { Sid::palmMuteFontSize,                      Pid::BEGIN_FONT_SIZE },
    { Sid::palmMuteFontSize,                      Pid::CONTINUE_FONT_SIZE },
    { Sid::palmMuteFontSize,                      Pid::END_FONT_SIZE },
    { Sid::palmMuteFontStyle,                     Pid::BEGIN_FONT_STYLE },
    { Sid::palmMuteFontStyle,                     Pid::CONTINUE_FONT_STYLE },
    { Sid::palmMuteFontStyle,                     Pid::END_FONT_STYLE },
    { Sid::palmMuteTextAlign,                     Pid::BEGIN_TEXT_ALIGN },
    { Sid::palmMuteTextAlign,                     Pid::CONTINUE_TEXT_ALIGN },
    { Sid::palmMuteTextAlign,                     Pid::END_TEXT_ALIGN },
    { Sid::palmMuteHookHeight,                    Pid::BEGIN_HOOK_HEIGHT },
    { Sid::palmMuteHookHeight,                    Pid::END_HOOK_HEIGHT },
    { Sid::palmMutePosBelow,                      Pid::OFFSET },
    { Sid::palmMuteLineStyle,                     Pid::LINE_STYLE },
    { Sid::palmMuteDashLineLen,                   Pid::DASH_LINE_LEN },
    { Sid::palmMuteDashGapLen,                    Pid::DASH_GAP_LEN },
    { Sid::palmMuteBeginTextOffset,               Pid::BEGIN_TEXT_OFFSET },
    { Sid::palmMuteEndHookType,                   Pid::END_HOOK_TYPE },
    { Sid::palmMuteLineWidth,                     Pid::LINE_WIDTH },
    { Sid::palmMutePlacement,                     Pid::PLACEMENT },
    { Sid::palmMutePosBelow,                      Pid::OFFSET },
};

PalmMuteSegment::PalmMuteSegment(PalmMute* sp, System* parent)
    : TextLineBaseSegment(ElementType::PALM_MUTE_SEGMENT, sp, parent, ElementFlag::MOVABLE | ElementFlag::ON_STAFF)
{
}

//---------------------------------------------------------
//   layout
//---------------------------------------------------------

void PalmMuteSegment::layout()
{
    const StaffType* stType = staffType();

    if (stType && stType->isHiddenElementOnTab(score(), Sid::palmMuteShowTabCommon, Sid::palmMuteShowTabSimple)) {
        setbbox(RectF());
        return;
    }

    TextLineBaseSegment::layout();
    autoplaceSpannerSegment();
}

//---------------------------------------------------------
//   getPropertyStyle
//---------------------------------------------------------

Sid PalmMuteSegment::getPropertyStyle(Pid pid) const
{
    if (pid == Pid::OFFSET) {
        return spanner()->placeAbove() ? Sid::palmMutePosAbove : Sid::palmMutePosBelow;
    }
    return TextLineBaseSegment::getPropertyStyle(pid);
}

Sid PalmMute::getPropertyStyle(Pid pid) const
{
    if (pid == Pid::OFFSET) {
        return placeAbove() ? Sid::palmMutePosAbove : Sid::palmMutePosBelow;
    }
    return TextLineBase::getPropertyStyle(pid);
}

//---------------------------------------------------------
//   PalmMute
//---------------------------------------------------------

PalmMute::PalmMute(EngravingItem* parent)
    : ChordTextLineBase(ElementType::PALM_MUTE, parent)
{
    initElementStyle(&palmMuteStyle);
    resetProperty(Pid::LINE_VISIBLE);

    resetProperty(Pid::BEGIN_TEXT_PLACE);
    resetProperty(Pid::BEGIN_TEXT);
    resetProperty(Pid::CONTINUE_TEXT_PLACE);
    resetProperty(Pid::CONTINUE_TEXT);
    resetProperty(Pid::END_TEXT_PLACE);
    resetProperty(Pid::END_TEXT);
}

//---------------------------------------------------------
//   read
//---------------------------------------------------------

void PalmMute::read(XmlReader& e)
{
    if (score()->mscVersion() < 301) {
        e.context()->addSpanner(e.intAttribute("id", -1), this);
    }
    while (e.readNextStartElement()) {
        if (readProperty(e.name(), e, Pid::LINE_WIDTH)) {
            setPropertyFlags(Pid::LINE_WIDTH, PropertyFlags::UNSTYLED);
        } else if (!TextLineBase::readProperties(e)) {
            e.unknown();
        }
    }
}

//---------------------------------------------------------
//   write
//
//   The removal of this function is potentially a temporary
//   change. For now, the intended behavior does no more than
//   the base write function and so we will just use that.
//
//   also see letring.cpp
//---------------------------------------------------------

/*
void PalmMute::write(XmlWriter& xml) const
      {
      if (!xml.context()->canWrite(this))
            return;
      xml.stag(this);

      for (const StyledProperty& spp : *styledProperties()) {
            if(!isStyled(spp.pid))
                  writeProperty(xml, spp.pid);
            }

      TextLineBase::writeProperties(xml);
      xml.etag();
      }
*/

//---------------------------------------------------------
//   createLineSegment
//---------------------------------------------------------

static const ElementStyle palmMuteSegmentStyle {
    { Sid::palmMutePosBelow,                      Pid::OFFSET },
    { Sid::palmMuteMinDistance,                   Pid::MIN_DISTANCE },
};

LineSegment* PalmMute::createLineSegment(System* parent)
{
    PalmMuteSegment* pms = new PalmMuteSegment(this, parent);
    pms->setTrack(track());
    pms->initElementStyle(&palmMuteSegmentStyle);
    return pms;
}

//---------------------------------------------------------
//   propertyDefault
//---------------------------------------------------------

PropertyValue PalmMute::propertyDefault(Pid propertyId) const
{
    switch (propertyId) {
    case Pid::LINE_WIDTH:
        return score()->styleV(Sid::palmMuteLineWidth);

    case Pid::ALIGN:
        return Align(AlignH::LEFT, AlignV::BASELINE);

    case Pid::LINE_STYLE:
        return score()->styleV(Sid::palmMuteLineStyle);

    case Pid::LINE_VISIBLE:
        return true;

    case Pid::CONTINUE_TEXT_OFFSET:
    case Pid::END_TEXT_OFFSET:
        return PropertyValue::fromValue(PointF(0, 0));

//TODOws            case Pid::BEGIN_FONT_ITALIC:
//                  return score()->styleV(Sid::palmMuteFontItalic);

    case Pid::BEGIN_TEXT:
        return score()->styleV(Sid::palmMuteText);
    case Pid::CONTINUE_TEXT:
    case Pid::END_TEXT:
        return "";

    case Pid::BEGIN_HOOK_TYPE:
        return HookType::NONE;

    case Pid::BEGIN_TEXT_PLACE:
    case Pid::CONTINUE_TEXT_PLACE:
    case Pid::END_TEXT_PLACE:
        return TextPlace::AUTO;

    default:
        return TextLineBase::propertyDefault(propertyId);
    }
}
}
