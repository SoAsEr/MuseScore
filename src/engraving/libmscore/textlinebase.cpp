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

#include "textlinebase.h"

#include <cmath>

#include "draw/pen.h"
#include "style/style.h"
#include "rw/xml.h"

#include "factory.h"
#include "system.h"
#include "measure.h"
#include "utils.h"
#include "score.h"
#include "text.h"
#include "mscore.h"
#include "staff.h"

#include "log.h"

using namespace mu;
using namespace mu::engraving;

namespace mu::engraving {
//---------------------------------------------------------
//   TextLineBaseSegment
//---------------------------------------------------------

TextLineBaseSegment::TextLineBaseSegment(const ElementType& type, Spanner* sp, System* parent, ElementFlags f)
    : LineSegment(type, sp, parent, f)
{
    _text    = Factory::createText(this, TextStyleType::DEFAULT, false);
    _endText = Factory::createText(this, TextStyleType::DEFAULT, false);
    _text->setParent(this);
    _endText->setParent(this);
    _text->setFlag(ElementFlag::MOVABLE, false);
    _endText->setFlag(ElementFlag::MOVABLE, false);
}

TextLineBaseSegment::TextLineBaseSegment(const TextLineBaseSegment& seg)
    : LineSegment(seg)
{
    _text    = seg._text->clone();
    _endText = seg._endText->clone();
    _text->setParent(this);
    _endText->setParent(this);
    layout();      // set the right _text
}

TextLineBaseSegment::~TextLineBaseSegment()
{
    delete _text;
    delete _endText;
}

//---------------------------------------------------------
//   setSelected
//---------------------------------------------------------

void TextLineBaseSegment::setSelected(bool f)
{
    SpannerSegment::setSelected(f);
    _text->setSelected(f);
    _endText->setSelected(f);
}

//---------------------------------------------------------
//   draw
//---------------------------------------------------------

void TextLineBaseSegment::draw(mu::draw::Painter* painter) const
{
    TRACE_OBJ_DRAW;
    using namespace mu::draw;
    TextLineBase* tl   = textLineBase();

    if (!_text->empty()) {
        painter->translate(_text->pos());
        _text->setVisible(tl->visible());
        _text->draw(painter);
        painter->translate(-_text->pos());
    }

    if (!_endText->empty()) {
        painter->translate(_endText->pos());
        _endText->setVisible(tl->visible());
        _endText->draw(painter);
        painter->translate(-_endText->pos());
    }

    if ((npoints == 0) || (score() && (score()->printing() || !score()->showInvisible()) && !tl->lineVisible())) {
        return;
    }

    // color for line (text color comes from the text properties)
    Color color = curColor(tl->visible() && tl->lineVisible(), tl->lineColor());

    double textlineLineWidth = tl->lineWidth();
    if (staff()) {
        textlineLineWidth *= mag();
    }

    Pen pen(color, textlineLineWidth);
    Pen solidPen(color, textlineLineWidth);

    switch (tl->lineStyle()) {
    case LineType::SOLID:
        break;
    case LineType::DASHED:
        pen.setDashPattern({ tl->dashLineLen(), tl->dashGapLen() });
        break;
    case LineType::DOTTED:
        pen.setDashPattern({ 0.01, 1.99 });
        pen.setCapStyle(PenCapStyle::RoundCap); // round dots
        break;
    }

    //Draw lines
    if (twoLines) { // hairpins
        painter->setPen(pen);
        painter->drawLines(&points[0], 1);
        painter->drawLines(&points[2], 1);
        return;
    }

    int start = 0;
    int end = npoints;
    //draw centered hooks as solid
    painter->setPen(solidPen);
    if (tl->beginHookType() == HookType::HOOK_90T && (isSingleType() || isBeginType())) {
        painter->drawLines(&points[0], 1);
        start++;
    }
    if (tl->endHookType() == HookType::HOOK_90T && (isSingleType() || isEndType())) {
        painter->drawLines(&points[npoints - 1], 1);
        end--;
    }
    //draw rest of line as regular
    //calculate new gap
    if (tl->lineStyle() == LineType::DASHED) {
        double adjustedLineLength = lineLength / textlineLineWidth;
        double dash = tl->dashLineLen();
        double gap = tl->dashGapLen();
        int numPairs;
        double newGap = 0;
        std::vector<double> nDashes { dash, newGap };
        if (tl->beginHookType() == HookType::HOOK_45 || tl->beginHookType() == HookType::HOOK_90) {
            double absD
                = sqrt(PointF::dotProduct(points[start + 1] - points[start], points[start + 1] - points[start])) / textlineLineWidth;
            numPairs = std::max(double(1), absD / (dash + gap));
            nDashes[1] = (absD - dash * (numPairs + 1)) / numPairs;
            pen.setDashPattern(nDashes);
            painter->setPen(pen);
            painter->drawLine(points[start + 1], points[start]);
            start++;
        }
        if (tl->endHookType() == HookType::HOOK_45 || tl->endHookType() == HookType::HOOK_90) {
            double absD = sqrt(PointF::dotProduct(points[end] - points[end - 1], points[end] - points[end - 1])) / textlineLineWidth;
            numPairs = std::max(double(1), absD / (dash + gap));
            nDashes[1] = (absD - dash * (numPairs + 1)) / numPairs;
            pen.setDashPattern(nDashes);
            painter->setPen(pen);
            painter->drawLines(&points[end - 1], 1);
            end--;
        }
        numPairs = std::max(double(1), adjustedLineLength / (dash + gap));
        nDashes[1] = (adjustedLineLength - dash * (numPairs + 1)) / numPairs;
        pen.setDashPattern(nDashes);
    }
    painter->setPen(pen);
    for (int i = start; i < end; ++i) {
        painter->drawLines(&points[i], 1);
    }
}

//---------------------------------------------------------
//   shape
//---------------------------------------------------------

Shape TextLineBaseSegment::shape() const
{
    Shape shape;
    if (!_text->empty()) {
        shape.add(_text->bbox().translated(_text->pos()));
    }
    if (!_endText->empty()) {
        shape.add(_endText->bbox().translated(_endText->pos()));
    }
    double lw  = textLineBase()->lineWidth();
    double lw2 = lw * .5;
    if (twoLines) {     // hairpins
        shape.add(RectF(points[0], points[1]).normalized().adjusted(-lw2, -lw2, lw2, lw2));
        shape.add(RectF(points[3], points[2]).normalized().adjusted(-lw2, -lw2, lw2, lw2));
    } else if (textLineBase()->lineVisible()) {
        for (int i = 0; i < npoints; ++i) {
            shape.add(RectF(points[i], points[i + 1]).normalized().adjusted(-lw2, -lw2, lw2, lw2));
        }
    }
    return shape;
}

bool TextLineBaseSegment::setProperty(Pid id, const PropertyValue& v)
{
    if (id == Pid::COLOR) {
        mu::draw::Color color = v.value<mu::draw::Color>();

        if (_text) {
            _text->setColor(color);
        }

        if (_endText) {
            _endText->setColor(color);
        }
    }

    return LineSegment::setProperty(id, v);
}

//---------------------------------------------------------
//   layout
//---------------------------------------------------------

void TextLineBaseSegment::layout()
{
    npoints      = 0;
    TextLineBase* tl = textLineBase();
    double _spatium = tl->spatium();

    if (spanner()->placeBelow()) {
        setPosY(staff() ? staff()->height() : 0.0);
    }

    // adjust Y pos to staffType offset
    if (staffType()) {
        movePosY(staffType()->yoffset().val() * spatium());
    }

    if (!tl->diagonal()) {
        _offset2.setY(0);
    }

    switch (spannerSegmentType()) {
    case SpannerSegmentType::SINGLE:
    case SpannerSegmentType::BEGIN:
        _text->setXmlText(tl->beginText());
        _text->setFamily(tl->beginFontFamily());
        _text->setSize(tl->beginFontSize());
        _text->setOffset(tl->beginTextOffset() * mag());
        _text->setAlign(tl->beginTextAlign());
        _text->setBold(tl->beginFontStyle() & FontStyle::Bold);
        _text->setItalic(tl->beginFontStyle() & FontStyle::Italic);
        _text->setUnderline(tl->beginFontStyle() & FontStyle::Underline);
        _text->setStrike(tl->beginFontStyle() & FontStyle::Strike);
        break;
    case SpannerSegmentType::MIDDLE:
    case SpannerSegmentType::END:
        _text->setXmlText(tl->continueText());
        _text->setFamily(tl->continueFontFamily());
        _text->setSize(tl->continueFontSize());
        _text->setOffset(tl->continueTextOffset() * mag());
        _text->setAlign(tl->continueTextAlign());
        _text->setBold(tl->continueFontStyle() & FontStyle::Bold);
        _text->setItalic(tl->continueFontStyle() & FontStyle::Italic);
        _text->setUnderline(tl->continueFontStyle() & FontStyle::Underline);
        _text->setStrike(tl->continueFontStyle() & FontStyle::Strike);
        break;
    }
    _text->setPlacement(PlacementV::ABOVE);
    _text->setTrack(track());
    _text->layout();

    if ((isSingleType() || isEndType())) {
        _endText->setXmlText(tl->endText());
        _endText->setFamily(tl->endFontFamily());
        _endText->setSize(tl->endFontSize());
        _endText->setOffset(tl->endTextOffset());
        _endText->setAlign(tl->endTextAlign());
        _endText->setBold(tl->endFontStyle() & FontStyle::Bold);
        _endText->setItalic(tl->endFontStyle() & FontStyle::Italic);
        _endText->setUnderline(tl->endFontStyle() & FontStyle::Underline);
        _endText->setStrike(tl->endFontStyle() & FontStyle::Strike);
        _endText->setPlacement(PlacementV::ABOVE);
        _endText->setTrack(track());
        _endText->layout();
    } else {
        _endText->setXmlText(u"");
    }

    PointF pp1;
    PointF pp2(pos2());

    // diagonal line with no text or hooks - just use the basic rectangle for line
    if (_text->empty() && _endText->empty() && pp2.y() != 0
        && textLineBase()->beginHookType() == HookType::NONE
        && textLineBase()->endHookType() == HookType::NONE) {
        npoints = 1;     // 2 points, but only one line must be drawn
        points[0] = pp1;
        points[1] = pp2;
        lineLength = sqrt(PointF::dotProduct(pp2 - pp1, pp2 - pp1));

        setbbox(RectF(pp1, pp2).normalized());
        return;
    }

    // line has text or hooks or is not diagonal - calculate reasonable bbox

    double x1 = std::min(0.0, pp2.x());
    double x2 = std::max(0.0, pp2.x());
    double y0 = -textLineBase()->lineWidth();
    double y1 = std::min(0.0, pp2.y()) + y0;
    double y2 = std::max(0.0, pp2.y()) - y0;

    double l = 0.0;
    if (!_text->empty()) {
        double textlineTextDistance = _spatium * .5;
        if (((isSingleType() || isBeginType())
             && (tl->beginTextPlace() == TextPlace::LEFT || tl->beginTextPlace() == TextPlace::AUTO))
            || ((isMiddleType() || isEndType()) && (tl->continueTextPlace() == TextPlace::LEFT))) {
            l = _text->pos().x() + _text->bbox().width() + textlineTextDistance;
        }
        double h = _text->height();
        if (textLineBase()->beginTextPlace() == TextPlace::ABOVE) {
            y1 = std::min(y1, -h);
        } else if (textLineBase()->beginTextPlace() == TextPlace::BELOW) {
            y2 = std::max(y2, h);
        } else {
            y1 = std::min(y1, -h * .5);
            y2 = std::max(y2, h * .5);
        }
        x2 = std::max(x2, _text->width());
    }

    if (textLineBase()->endHookType() != HookType::NONE) {
        double h = pp2.y() + textLineBase()->endHookHeight().val() * _spatium;
        if (h > y2) {
            y2 = h;
        } else if (h < y1) {
            y1 = h;
        }
    }

    if (textLineBase()->beginHookType() != HookType::NONE) {
        double h = textLineBase()->beginHookHeight().val() * _spatium;
        if (h > y2) {
            y2 = h;
        } else if (h < y1) {
            y1 = h;
        }
    }
    bbox().setRect(x1, y1, x2 - x1, y2 - y1);
    if (!_text->empty()) {
        bbox() |= _text->bbox().translated(_text->pos());      // DEBUG
    }
    // set end text position and extend bbox
    if (!_endText->empty()) {
        _endText->setPos(bbox().right(), 0);
        bbox() |= _endText->bbox().translated(_endText->pos());
    }

    if (!(tl->lineVisible() || score()->showInvisible())) {
        return;
    }

    if (tl->lineVisible() || !score()->printing()) {
        pp1 = PointF(l, 0.0);

        double beginHookWidth;
        double endHookWidth;

        if (tl->beginHookType() == HookType::HOOK_45) {
            beginHookWidth = fabs(tl->beginHookHeight().val() * _spatium * .4);
            pp1.rx() += beginHookWidth;
        } else {
            beginHookWidth = 0;
        }

        if (tl->endHookType() == HookType::HOOK_45) {
            endHookWidth = fabs(tl->endHookHeight().val() * _spatium * .4);
            pp2.rx() -= endHookWidth;
        } else {
            endHookWidth = 0;
        }

        // don't draw backwards lines (or hooks) if text is longer than nominal line length
        bool backwards = !_text->empty() && pp1.x() > pp2.x() && !tl->diagonal();

        if ((tl->beginHookType() != HookType::NONE) && (isSingleType() || isBeginType())) {
            double hh = tl->beginHookHeight().val() * _spatium;
            if (tl->beginHookType() == HookType::HOOK_90T) {
                points[npoints++] = PointF(pp1.x() - beginHookWidth, pp1.y() - hh);
            }
            points[npoints] = PointF(pp1.x() - beginHookWidth, pp1.y() + hh);
            ++npoints;
            points[npoints] = pp1;
        }
        if (!backwards) {
            points[npoints] = pp1;
            ++npoints;
            points[npoints] = pp2;
            lineLength = sqrt(PointF::dotProduct(pp2 - pp1, pp2 - pp1));
            // painter->drawLine(LineF(pp1.x(), pp1.y(), pp2.x(), pp2.y()));

            if ((tl->endHookType() != HookType::NONE) && (isSingleType() || isEndType())) {
                ++npoints;
                double hh = tl->endHookHeight().val() * _spatium;
                // painter->drawLine(LineF(pp2.x(), pp2.y(), pp2.x() + endHookWidth, pp2.y() + hh));
                points[npoints] = PointF(pp2.x() + endHookWidth, pp2.y() + hh);
                if (tl->endHookType() == HookType::HOOK_90T) {
                    points[++npoints] = PointF(pp2.x() + endHookWidth, pp2.y() - hh);
                }
            }
        }
    }
}

//---------------------------------------------------------
//   spatiumChanged
//---------------------------------------------------------

void TextLineBaseSegment::spatiumChanged(double ov, double nv)
{
    LineSegment::spatiumChanged(ov, nv);

    textLineBase()->spatiumChanged(ov, nv);
    _text->spatiumChanged(ov, nv);
    _endText->spatiumChanged(ov, nv);
}

static constexpr std::array<Pid, 26> TextLineBasePropertyId = { {
    Pid::LINE_VISIBLE,
    Pid::BEGIN_HOOK_TYPE,
    Pid::BEGIN_HOOK_HEIGHT,
    Pid::END_HOOK_TYPE,
    Pid::END_HOOK_HEIGHT,
    Pid::BEGIN_TEXT,
    Pid::BEGIN_TEXT_ALIGN,
    Pid::BEGIN_TEXT_PLACE,
    Pid::BEGIN_FONT_FACE,
    Pid::BEGIN_FONT_SIZE,
    Pid::BEGIN_FONT_STYLE,
    Pid::BEGIN_TEXT_OFFSET,
    Pid::CONTINUE_TEXT,
    Pid::CONTINUE_TEXT_ALIGN,
    Pid::CONTINUE_TEXT_PLACE,
    Pid::CONTINUE_FONT_FACE,
    Pid::CONTINUE_FONT_SIZE,
    Pid::CONTINUE_FONT_STYLE,
    Pid::CONTINUE_TEXT_OFFSET,
    Pid::END_TEXT,
    Pid::END_TEXT_ALIGN,
    Pid::END_TEXT_PLACE,
    Pid::END_FONT_FACE,
    Pid::END_FONT_SIZE,
    Pid::END_FONT_STYLE,
    Pid::END_TEXT_OFFSET,
} };

//---------------------------------------------------------
//   propertyDelegate
//---------------------------------------------------------

EngravingItem* TextLineBaseSegment::propertyDelegate(Pid pid)
{
    for (Pid id : TextLineBasePropertyId) {
        if (pid == id) {
            return spanner();
        }
    }
    return LineSegment::propertyDelegate(pid);
}

//---------------------------------------------------------
//   TextLineBase
//---------------------------------------------------------

TextLineBase::TextLineBase(const ElementType& type, EngravingItem* parent, ElementFlags f)
    : SLine(type, parent, f)
{
    setBeginHookHeight(Spatium(1.9));
    setEndHookHeight(Spatium(1.9));
}

//---------------------------------------------------------
//   write
//---------------------------------------------------------

void TextLineBase::write(XmlWriter& xml) const
{
    if (!xml.context()->canWrite(this)) {
        return;
    }
    xml.startElement(this);
    writeProperties(xml);
    xml.endElement();
}

//---------------------------------------------------------
//   read
//---------------------------------------------------------

void TextLineBase::read(XmlReader& e)
{
    eraseSpannerSegments();

    if (score()->mscVersion() < 301) {
        e.context()->addSpanner(e.intAttribute("id", -1), this);
    }

    while (e.readNextStartElement()) {
        if (!readProperties(e)) {
            e.unknown();
        }
    }
}

//---------------------------------------------------------
//   spatiumChanged
//---------------------------------------------------------

void TextLineBase::spatiumChanged(double /*ov*/, double /*nv*/)
{
}

//---------------------------------------------------------
//   writeProperties
//    write properties different from prototype
//---------------------------------------------------------

void TextLineBase::writeProperties(XmlWriter& xml) const
{
    for (Pid pid : TextLineBasePropertyId) {
        if (!isStyled(pid)) {
            writeProperty(xml, pid);
        }
    }
    SLine::writeProperties(xml);
}

//---------------------------------------------------------
//   readProperties
//---------------------------------------------------------

bool TextLineBase::readProperties(XmlReader& e)
{
    const AsciiStringView tag(e.name());
    for (Pid i : TextLineBasePropertyId) {
        if (readProperty(tag, e, i)) {
            setPropertyFlags(i, PropertyFlags::UNSTYLED);
            return true;
        }
    }
    return SLine::readProperties(e);
}

//---------------------------------------------------------
//   getProperty
//---------------------------------------------------------

PropertyValue TextLineBase::getProperty(Pid id) const
{
    switch (id) {
    case Pid::BEGIN_TEXT:
        return beginText();
    case Pid::BEGIN_TEXT_ALIGN:
        return PropertyValue::fromValue(beginTextAlign());
    case Pid::CONTINUE_TEXT_ALIGN:
        return PropertyValue::fromValue(continueTextAlign());
    case Pid::END_TEXT_ALIGN:
        return PropertyValue::fromValue(endTextAlign());
    case Pid::BEGIN_TEXT_PLACE:
        return _beginTextPlace;
    case Pid::BEGIN_HOOK_TYPE:
        return _beginHookType;
    case Pid::BEGIN_HOOK_HEIGHT:
        return _beginHookHeight;
    case Pid::BEGIN_FONT_FACE:
        return _beginFontFamily;
    case Pid::BEGIN_FONT_SIZE:
        return _beginFontSize;
    case Pid::BEGIN_FONT_STYLE:
        return int(_beginFontStyle);
    case Pid::BEGIN_TEXT_OFFSET:
        return _beginTextOffset;
    case Pid::CONTINUE_TEXT:
        return continueText();
    case Pid::CONTINUE_TEXT_PLACE:
        return _continueTextPlace;
    case Pid::CONTINUE_FONT_FACE:
        return _continueFontFamily;
    case Pid::CONTINUE_FONT_SIZE:
        return _continueFontSize;
    case Pid::CONTINUE_FONT_STYLE:
        return int(_continueFontStyle);
    case Pid::CONTINUE_TEXT_OFFSET:
        return _continueTextOffset;
    case Pid::END_TEXT:
        return endText();
    case Pid::END_TEXT_PLACE:
        return _endTextPlace;
    case Pid::END_HOOK_TYPE:
        return _endHookType;
    case Pid::END_HOOK_HEIGHT:
        return _endHookHeight;
    case Pid::END_FONT_FACE:
        return _endFontFamily;
    case Pid::END_FONT_SIZE:
        return _endFontSize;
    case Pid::END_FONT_STYLE:
        return int(_endFontStyle);
    case Pid::END_TEXT_OFFSET:
        return _endTextOffset;
    case Pid::LINE_VISIBLE:
        return lineVisible();
    default:
        return SLine::getProperty(id);
    }
}

//---------------------------------------------------------
//   setProperty
//---------------------------------------------------------

bool TextLineBase::setProperty(Pid id, const PropertyValue& v)
{
    switch (id) {
    case Pid::BEGIN_TEXT_PLACE:
        _beginTextPlace = v.value<TextPlace>();
        break;
    case Pid::BEGIN_TEXT_ALIGN:
        _beginTextAlign = v.value<Align>();
        break;
    case Pid::CONTINUE_TEXT_ALIGN:
        _continueTextAlign = v.value<Align>();
        break;
    case Pid::END_TEXT_ALIGN:
        _endTextAlign = v.value<Align>();
        break;
    case Pid::CONTINUE_TEXT_PLACE:
        _continueTextPlace = v.value<TextPlace>();
        break;
    case Pid::END_TEXT_PLACE:
        _endTextPlace = v.value<TextPlace>();
        break;
    case Pid::BEGIN_HOOK_HEIGHT:
        _beginHookHeight = v.value<Spatium>();
        break;
    case Pid::END_HOOK_HEIGHT:
        _endHookHeight = v.value<Spatium>();
        break;
    case Pid::BEGIN_HOOK_TYPE:
        _beginHookType = v.value<HookType>();
        break;
    case Pid::END_HOOK_TYPE:
        _endHookType = v.value<HookType>();
        break;
    case Pid::BEGIN_TEXT:
        setBeginText(v.value<String>());
        break;
    case Pid::BEGIN_TEXT_OFFSET:
        setBeginTextOffset(v.value<PointF>());
        break;
    case Pid::CONTINUE_TEXT_OFFSET:
        setContinueTextOffset(v.value<PointF>());
        break;
    case Pid::END_TEXT_OFFSET:
        setEndTextOffset(v.value<PointF>());
        break;
    case Pid::CONTINUE_TEXT:
        setContinueText(v.value<String>());
        break;
    case Pid::END_TEXT:
        setEndText(v.value<String>());
        break;
    case Pid::LINE_VISIBLE:
        setLineVisible(v.toBool());
        break;
    case Pid::BEGIN_FONT_FACE:
        setBeginFontFamily(v.value<String>());
        break;
    case Pid::BEGIN_FONT_SIZE:
        if (v.toReal() <= 0) {
            ASSERT_X(String(u"font size is %1").arg(v.toReal()));
        }
        setBeginFontSize(v.toReal());
        break;
    case Pid::BEGIN_FONT_STYLE:
        setBeginFontStyle(FontStyle(v.toInt()));
        break;
    case Pid::CONTINUE_FONT_FACE:
        setContinueFontFamily(v.value<String>());
        break;
    case Pid::CONTINUE_FONT_SIZE:
        setContinueFontSize(v.toReal());
        break;
    case Pid::CONTINUE_FONT_STYLE:
        setContinueFontStyle(FontStyle(v.toInt()));
        break;
    case Pid::END_FONT_FACE:
        setEndFontFamily(v.value<String>());
        break;
    case Pid::END_FONT_SIZE:
        setEndFontSize(v.toReal());
        break;
    case Pid::END_FONT_STYLE:
        setEndFontStyle(FontStyle(v.toInt()));
        break;
    default:
        return SLine::setProperty(id, v);
    }
    triggerLayout();
    return true;
}
}
