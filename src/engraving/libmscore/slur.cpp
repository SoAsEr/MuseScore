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
#include "slur.h"

#include <cmath>

#include "draw/transform.h"
#include "draw/pen.h"
#include "draw/brush.h"
#include "rw/xml.h"

#include "articulation.h"
#include "chord.h"
#include "measure.h"
#include "mscoreview.h"
#include "navigate.h"
#include "part.h"
#include "score.h"
#include "stem.h"
#include "system.h"
#include "tie.h"
#include "undo.h"
#include "hook.h"

// included for gonville/musejazz hook hack in SlurPos
#include "scorefont.h"

#include "log.h"

using namespace mu;
using namespace mu::engraving;
using namespace mu::draw;

namespace mu::engraving {
SlurSegment::SlurSegment(System* parent)
    : SlurTieSegment(ElementType::SLUR_SEGMENT, parent)
{
}

SlurSegment::SlurSegment(const SlurSegment& ss)
    : SlurTieSegment(ss)
{
}

//---------------------------------------------------------
//   draw
//---------------------------------------------------------

void SlurSegment::draw(mu::draw::Painter* painter) const
{
    TRACE_OBJ_DRAW;
    using namespace mu::draw;
    Pen pen(curColor());
    double mag = staff() ? staff()->staffMag(slur()->tick()) : 1.0;

    //Replace generic Qt dash patterns with improved equivalents to show true dots (keep in sync with tie.cpp)
    std::vector<double> dotted     = { 0.01, 1.99 };   // tighter than Qt PenStyle::DotLine equivalent - would be { 0.01, 2.99 }
    std::vector<double> dashed     = { 3.00, 3.00 };   // Compensating for caps. Qt default PenStyle::DashLine is { 4.0, 2.0 }
    std::vector<double> wideDashed = { 5.00, 6.00 };

    switch (slurTie()->styleType()) {
    case SlurStyleType::Solid:
        painter->setBrush(Brush(pen.color()));
        pen.setCapStyle(PenCapStyle::RoundCap);
        pen.setJoinStyle(PenJoinStyle::RoundJoin);
        pen.setWidthF(score()->styleMM(Sid::SlurEndWidth) * mag);
        break;
    case SlurStyleType::Dotted:
        painter->setBrush(BrushStyle::NoBrush);
        pen.setCapStyle(PenCapStyle::RoundCap);           // round dots
        pen.setDashPattern(dotted);
        pen.setWidthF(score()->styleMM(Sid::SlurDottedWidth) * mag);
        break;
    case SlurStyleType::Dashed:
        painter->setBrush(BrushStyle::NoBrush);
        pen.setDashPattern(dashed);
        pen.setWidthF(score()->styleMM(Sid::SlurDottedWidth) * mag);
        break;
    case SlurStyleType::WideDashed:
        painter->setBrush(BrushStyle::NoBrush);
        pen.setDashPattern(wideDashed);
        pen.setWidthF(score()->styleMM(Sid::SlurDottedWidth) * mag);
        break;
    case SlurStyleType::Undefined:
        break;
    }
    painter->setPen(pen);
    painter->drawPath(path);
}

//---------------------------------------------------------
//   searchCR
//---------------------------------------------------------

static ChordRest* searchCR(Segment* segment, track_idx_t startTrack, track_idx_t endTrack)
{
    // for (Segment* s = segment; s; s = s->next1MM(SegmentType::ChordRest)) {
    for (Segment* s = segment; s; s = s->next(SegmentType::ChordRest)) {       // restrict search to measure
        if (startTrack > endTrack) {
            for (int t = static_cast<int>(startTrack) - 1; t >= static_cast<int>(endTrack); --t) {
                if (s->element(t)) {
                    return toChordRest(s->element(t));
                }
            }
        } else {
            for (track_idx_t t = startTrack; t < endTrack; ++t) {
                if (s->element(t)) {
                    return toChordRest(s->element(t));
                }
            }
        }
    }
    return 0;
}

bool SlurSegment::isEditAllowed(EditData& ed) const
{
    if (ed.key == Key_X && !ed.modifiers) {
        return true;
    }

    if (ed.key == Key_Home && !ed.modifiers) {
        return true;
    }

    const bool moveStart = ed.curGrip == Grip::START;
    const bool moveEnd = ed.curGrip == Grip::END || ed.curGrip == Grip::DRAG;

    if (!((ed.modifiers & ShiftModifier) && (isSingleType()
                                             || (isBeginType() && moveStart) || (isEndType() && moveEnd)))) {
        return false;
    }

    static const std::set<int> navigationKeys {
        Key_Left,
        Key_Up,
        Key_Down,
        Key_Right
    };

    return mu::contains(navigationKeys, ed.key);
}

//---------------------------------------------------------
//   edit
//    return true if event is accepted
//---------------------------------------------------------

bool SlurSegment::edit(EditData& ed)
{
    if (!isEditAllowed(ed)) {
        return false;
    }

    Slur* sl = slur();

    if (ed.key == Key_X && !ed.modifiers) {
        sl->undoChangeProperty(Pid::SLUR_DIRECTION, PropertyValue::fromValue<DirectionV>(sl->up() ? DirectionV::DOWN : DirectionV::UP));
        sl->layout();
        return true;
    }
    if (ed.key == Key_Home && !ed.modifiers) {
        ups(ed.curGrip).off = PointF();
        sl->layout();
        return true;
    }

    ChordRest* cr = 0;
    ChordRest* e;
    ChordRest* e1;
    if (ed.curGrip == Grip::START) {
        e  = sl->startCR();
        e1 = sl->endCR();
    } else {
        e  = sl->endCR();
        e1 = sl->startCR();
    }

    if (ed.key == Key_Left) {
        cr = prevChordRest(e);
    } else if (ed.key == Key_Right) {
        cr = nextChordRest(e);
    } else if (ed.key == Key_Up) {
        Part* part     = e->part();
        track_idx_t startTrack = part->startTrack();
        track_idx_t endTrack   = e->track();
        cr = searchCR(e->segment(), endTrack, startTrack);
    } else if (ed.key == Key_Down) {
        track_idx_t startTrack = e->track() + 1;
        Part* part     = e->part();
        track_idx_t endTrack   = part->endTrack();
        cr = searchCR(e->segment(), startTrack, endTrack);
    } else {
        return false;
    }
    if (cr && cr != e1) {
        changeAnchor(ed, cr);
    }
    return true;
}

//---------------------------------------------------------
//   changeAnchor
//---------------------------------------------------------

void SlurSegment::changeAnchor(EditData& ed, EngravingItem* element)
{
    ChordRest* cr = element->isChordRest() ? toChordRest(element) : nullptr;
    ChordRest* scr = spanner()->startCR();
    ChordRest* ecr = spanner()->endCR();
    if (!cr || !scr || !ecr) {
        return;
    }

    // save current start/end elements
    for (EngravingObject* e : spanner()->linkList()) {
        Spanner* sp = toSpanner(e);
        score()->undoStack()->push1(new ChangeStartEndSpanner(sp, sp->startElement(), sp->endElement()));
    }

    if (ed.curGrip == Grip::START) {
        spanner()->undoChangeProperty(Pid::SPANNER_TICK, cr->tick());
        Fraction ticks = ecr->tick() - cr->tick();
        spanner()->undoChangeProperty(Pid::SPANNER_TICKS, ticks);
        int diff = static_cast<int>(cr->track() - spanner()->track());
        for (auto e : spanner()->linkList()) {
            Spanner* s = toSpanner(e);
            s->undoChangeProperty(Pid::TRACK, s->track() + diff);
        }
        scr = cr;
    } else {
        Fraction ticks = cr->tick() - scr->tick();
        spanner()->undoChangeProperty(Pid::SPANNER_TICKS, ticks);
        int diff = static_cast<int>(cr->track() - spanner()->track());
        for (auto e : spanner()->linkList()) {
            Spanner* s = toSpanner(e);
            s->undoChangeProperty(Pid::SPANNER_TRACK2, s->track() + diff);
        }
        ecr = cr;
    }

    // update start/end elements (which could be grace notes)
    for (EngravingObject* lsp : spanner()->linkList()) {
        Spanner* sp = static_cast<Spanner*>(lsp);
        if (sp == spanner()) {
            score()->undo(new ChangeSpannerElements(sp, scr, ecr));
        } else {
            EngravingItem* se = 0;
            EngravingItem* ee = 0;
            if (scr) {
                std::list<EngravingObject*> sel = scr->linkList();
                for (EngravingObject* lcr : sel) {
                    EngravingItem* le = toEngravingItem(lcr);
                    if (le->score() == sp->score() && le->track() == sp->track()) {
                        se = le;
                        break;
                    }
                }
            }
            if (ecr) {
                std::list<EngravingObject*> sel = ecr->linkList();
                for (EngravingObject* lcr : sel) {
                    EngravingItem* le = toEngravingItem(lcr);
                    if (le->score() == sp->score() && le->track() == sp->track2()) {
                        ee = le;
                        break;
                    }
                }
            }
            score()->undo(new ChangeStartEndSpanner(sp, se, ee));
            sp->layout();
        }
    }

    const size_t segments  = spanner()->spannerSegments().size();
    ups(ed.curGrip).off = PointF();
    spanner()->layout();
    if (spanner()->spannerSegments().size() != segments) {
        const std::vector<SpannerSegment*>& ss = spanner()->spannerSegments();
        const bool moveEnd = ed.curGrip == Grip::END || ed.curGrip == Grip::DRAG;
        SlurSegment* newSegment = toSlurSegment(moveEnd ? ss.back() : ss.front());
        ed.view()->changeEditElement(newSegment);
        triggerLayout();
    }
}

//---------------------------------------------------------
//   adjustEndpoints
//    move endpoints so as not to collide with staff lines
//---------------------------------------------------------
void SlurSegment::adjustEndpoints()
{
    const double staffLineMargin = 0.15;
    PointF p1 = ups(Grip::START).p;
    PointF p2 = ups(Grip::END).p;

    double y1sp = p1.y() / spatium();
    double y2sp = p2.y() / spatium();

    // point 1
    int lines = staff()->lines(tick());
    auto adjustPoint = [staffLineMargin](bool up, double ysp) {
        double y1offset = ysp - floor(ysp);
        double adjust = 0;
        if (up) {
            if (y1offset < staffLineMargin) {
                // endpoint too close to the line above
                adjust = -(y1offset + staffLineMargin);
            } else if (y1offset > 1 - staffLineMargin) {
                // endpoint too close to the line below
                adjust = -(y1offset - (1 - staffLineMargin));
            }
        } else {
            if (y1offset < staffLineMargin) {
                // endpoint too close to the line above
                adjust = staffLineMargin - y1offset;
            }
            if (y1offset > 1 - staffLineMargin) {
                // endpoint too close to the line below
                adjust = (1 - y1offset) + staffLineMargin;
            }
        }
        return adjust;
    };
    if (y1sp > -staffLineMargin && y1sp < (lines - 1) + staffLineMargin) {
        ups(Grip::START).p.ry() += adjustPoint(slur()->up(), y1sp) * spatium();
    }
    if (y2sp > -staffLineMargin && y2sp < (lines - 1) + staffLineMargin) {
        ups(Grip::END).p.ry() += adjustPoint(slur()->up(), y2sp) * spatium();
    }
}

//---------------------------------------------------------
//   computeBezier
//    compute help points of slur bezier segment
//---------------------------------------------------------

void SlurSegment::computeBezier(mu::PointF p6o)
{
    double _spatium  = spatium();
    double shoulderW;                // height as fraction of slur-length
    double shoulderH;
    if (autoplace()) {
        adjustEndpoints();
    }
    //
    // pp1 and pp2 are the end points of the slur
    //
    PointF pp1 = ups(Grip::START).p + ups(Grip::START).off;
    PointF pp2 = ups(Grip::END).p + ups(Grip::END).off;

    PointF p2 = pp2 - pp1;
    if ((p2.x() == 0.0) && (p2.y() == 0.0)) {
        Measure* m1 = slur()->startCR()->segment()->measure();
        Measure* m2 = slur()->endCR()->segment()->measure();
        LOGD("zero slur at tick %d(%d) track %zu in measure %d-%d  tick %d ticks %d",
             m1->tick().ticks(), tick().ticks(), track(), m1->no(), m2->no(), slur()->tick().ticks(), slur()->ticks().ticks());
        slur()->setBroken(true);
        return;
    }
    pp1 = ups(Grip::START).p + ups(Grip::START).off;
    pp2 = ups(Grip::END).p + ups(Grip::END).off;
    double sinb = atan(p2.y() / p2.x());
    Transform t;
    t.rotateRadians(-sinb);
    p2  = t.map(p2);
    p6o = t.map(p6o);

    double smallH = 0.5;
    double d = p2.x() / _spatium;
    if (d <= 2.0) {
        shoulderH = d * 0.5 * smallH * _spatium;
        shoulderW = .6;
    } else {
        double dd = log10(1.0 + (d - 2.0) * .5) * 2.0;
        if (dd > 3.0) {
            dd = 3.0;
        }
        shoulderH = (dd + smallH) * _spatium + _extraHeight;
        if (d > 18.0) {
            shoulderW = 0.7;       // 0.8;
        } else if (d > 10) {
            shoulderW = 0.6;       // 0.7;
        } else {
            shoulderW = 0.5;       // 0.6;
        }
    }

    shoulderH -= p6o.y();

    if (!slur()->up()) {
        shoulderH = -shoulderH;
    }

    double c    = p2.x();
    double c1   = (c - c * shoulderW) * .5 + p6o.x();
    double c2   = c1 + c * shoulderW + p6o.x();

    PointF p5 = PointF(c * .5, 0.0);

    PointF p3(c1, -shoulderH);
    PointF p4(c2, -shoulderH);

    double w = score()->styleMM(Sid::SlurMidWidth) - score()->styleMM(Sid::SlurEndWidth);
    if (staff()) {
        w *= staff()->staffMag(slur()->tick());
    }
    if ((c2 - c1) <= _spatium) {
        w *= .5;
    }
    PointF th(0.0, w);      // thickness of slur

    PointF p3o = p6o + t.map(ups(Grip::BEZIER1).off);
    PointF p4o = p6o + t.map(ups(Grip::BEZIER2).off);

    if (!p6o.isNull()) {
        PointF p6i = t.inverted().map(p6o);
        ups(Grip::BEZIER1).off += p6i;
        ups(Grip::BEZIER2).off += p6i;
    }

    //-----------------------------------calculate p6
    PointF pp3  = p3 + p3o;
    PointF pp4  = p4 + p4o;
    PointF ppp4 = pp4 - pp3;

    double r2 = atan(ppp4.y() / ppp4.x());
    t.reset();
    t.rotateRadians(-r2);
    PointF p6  = PointF(t.map(ppp4).x() * .5, 0.0);

    t.rotateRadians(2 * r2);
    p6 = t.map(p6) + pp3 - p6o;
    //-----------------------------------

    path = PainterPath();
    path.moveTo(PointF());
    path.cubicTo(p3 + p3o - th, p4 + p4o - th, p2);
    if (slur()->styleType() == SlurStyleType::Solid) {
        path.cubicTo(p4 + p4o + th, p3 + p3o + th, PointF());
    }

    th = PointF(0.0, 3.0 * w);
    shapePath = PainterPath();
    shapePath.moveTo(PointF());
    shapePath.cubicTo(p3 + p3o - th, p4 + p4o - th, p2);
    shapePath.cubicTo(p4 + p4o + th, p3 + p3o + th, PointF());

    // translate back
    t.reset();
    t.translate(pp1.x(), pp1.y());
    t.rotateRadians(sinb);
    path                  = t.map(path);
    shapePath             = t.map(shapePath);
    ups(Grip::BEZIER1).p  = t.map(p3);
    ups(Grip::BEZIER2).p  = t.map(p4);
    ups(Grip::END).p      = t.map(p2) - ups(Grip::END).off;
    ups(Grip::DRAG).p     = t.map(p5);
    ups(Grip::SHOULDER).p = t.map(p6);

    _shape.clear();
    PointF start = pp1;
    int nbShapes  = 32;    // (pp2.x() - pp1.x()) / _spatium;
    double minH    = std::abs(3 * w);
    const CubicBezier b(pp1, ups(Grip::BEZIER1).pos(), ups(Grip::BEZIER2).pos(), ups(Grip::END).pos());
    for (int i = 1; i <= nbShapes; i++) {
        const PointF point = b.pointAtPercent(i / float(nbShapes));
        RectF re     = RectF(start, point).normalized();
        if (re.height() < minH) {
            double d1 = (minH - re.height()) * .5;
            re.adjust(0.0, -d1, 0.0, d1);
        }
        _shape.add(re);
        start = point;
    }
}

//---------------------------------------------------------
//   layoutSegment
//---------------------------------------------------------

void SlurSegment::layoutSegment(const PointF& p1, const PointF& p2)
{
    const StaffType* stType = staffType();

    if (stType && stType->isHiddenElementOnTab(score(), Sid::slurShowTabCommon, Sid::slurShowTabSimple)) {
        setbbox(RectF());
        return;
    }

    setPos(PointF());
    ups(Grip::START).p = p1;
    ups(Grip::END).p   = p2;
    _extraHeight = 0.0;

    //Adjust Y pos to staff type yOffset before other calculations
    if (staffType()) {
        movePosY(staffType()->yoffset().val() * spatium());
    }

    computeBezier();

    if (autoplace() && system()) {
        const double maxHeightAdjust = 4 * spatium();
        const double maxEndpointAdjust = 3 * spatium();
        const double slurEndSectionPercent = 0.3;

        bool up = slur()->up();
        Segment* ls = system()->lastMeasure()->last();
        Segment* fs = system()->firstMeasure()->first();
        Segment* ss = slur()->startSegment();
        Segment* es = slur()->endSegment();
        PointF pp1 = ups(Grip::START).p;
        PointF pp2 = ups(Grip::END).p;
        double slurWidth = pp2.x() - pp1.x();
        double midpointDist = 0.0;
        double end1Dist = 0.0;
        double end2Dist = 0.0;
        double segRelativeX = 0.0;
        bool intersection = false;
        bool adjusted[3] = { false, false, false };
        const double collisionMargin = 0.5 * spatium();
        for (int tries = 0; tries < 3; ++tries) {
            intersection = false;
            end1Dist = end2Dist = midpointDist = 0.0;
            if (adjusted[0] && adjusted[1] && adjusted[2]) {
                adjusted[0] = adjusted[1] = adjusted[2] = false;
            }
            for (Segment* s = fs; s && s != ls; s = s->next1()) {
                if (!s->enabled()) {
                    continue;
                }
                // skip start and end segments on assumption start and end points were placed well already
                // this avoids overcorrection on collision with own ledger lines and accidentals
                // it also avoids issues where slur appears to be attached to a note in a different voice
                if (s == ss || s == es) {
                    continue;
                }
                // allow slurs to cross barlines
                if (s->segmentType() & SegmentType::BarLineType) {
                    continue;
                }
                double x1 = s->x() + s->measure()->x();
                double x2 = x1 + s->width();
                if (pp1.x() > x2) {
                    continue;
                }
                if (pp2.x() < x1) {
                    break;
                }
                const Shape& segShape = s->staffShape(staffIdx()).translated(s->pos() + s->measure()->pos());
                segRelativeX = ((x1 + (s->width() / 2)) - pp1.x()) / slurWidth;

                if (segShape.intersects(_shape)) {
                    intersection = true;

                    double dist = 0.0;
                    if (up) {
                        dist = _shape.minVerticalDistance(segShape) + collisionMargin;
                        dist += (y() - s->y()) / 1.5;
                    } else {
                        dist = segShape.minVerticalDistance(_shape) + collisionMargin;
                        dist += (s->y() - y()) / 1.5;
                    }
                    if (dist > 0.0) {
                        if (segRelativeX < slurEndSectionPercent) {
                            // collision in the first third
                            end1Dist = std::min(std::max(end1Dist, dist), maxEndpointAdjust);
                        } else if (segRelativeX > (1 - slurEndSectionPercent)) {
                            // collision in the final third
                            end2Dist = std::min(std::max(end2Dist, dist), maxEndpointAdjust);
                        } else {
                            // collision in the middle third
                            midpointDist = std::min(std::max(midpointDist, dist), maxHeightAdjust);
                        }
                    }
                }
            }
            if (!intersection) {
                break;
            }
            double maxDist = std::max(std::max(end1Dist, end2Dist), midpointDist);
            // find the worst collision:
            if (maxDist == end1Dist) {
                // move first endpoint
                if (!adjusted[0]) {
                    ups(Grip::START).p.ry() += end1Dist * (up ? -1 : 1);
                    adjusted[0] = true;
                } else if (!adjusted[1]) {
                    _extraHeight = 4 * std::min(end1Dist, maxHeightAdjust) / 3;
                    adjusted[1] = true;
                } else if (!adjusted[2]) {
                    ups(Grip::END).p.ry() += end1Dist * (up ? -1 : 1);
                    adjusted[2] = true;
                }
            } else if (maxDist == end2Dist) {
                // move second endpoint
                if (!adjusted[2]) {
                    ups(Grip::END).p.ry() += end2Dist * (up ? -1 : 1);
                    adjusted[2] = true;
                } else if (!adjusted[1]) {
                    _extraHeight = 4 * std::min(end2Dist, maxHeightAdjust) / 3;
                    adjusted[1] = true;
                } else if (!adjusted[0]) {
                    ups(Grip::START).p.ry() += end2Dist * (up ? -1 : 1);
                    adjusted[0] = true;
                }
            } else if (maxDist == midpointDist) {
                // make slur taller
                _extraHeight = 4 * midpointDist / 3;
                if (!adjusted[1]) {
                    _extraHeight = 4 * midpointDist / 3;
                    adjusted[1] = true;
                } else {
                    if (segRelativeX < .5) {
                        if (!adjusted[0]) {
                            ups(Grip::START).p.ry() += std::min(midpointDist, maxHeightAdjust) * (up ? -1 : 1);
                            adjusted[0] = true;
                        } else {
                            ups(Grip::END).p.ry() += std::min(midpointDist, maxHeightAdjust) * (up ? -1 : 1);
                            adjusted[2] = true;
                        }
                    } else {
                        if (!adjusted[2]) {
                            ups(Grip::END).p.ry() += std::min(midpointDist, maxHeightAdjust) * (up ? -1 : 1);
                            adjusted[2] = true;
                        } else {
                            ups(Grip::START).p.ry() += std::min(midpointDist, maxHeightAdjust) * (up ? -1 : 1);
                            adjusted[0] = true;
                        }
                    }
                }
            }
            computeBezier();
        }
    }
    setbbox(path.boundingRect());
}

//---------------------------------------------------------
//   isEdited
//---------------------------------------------------------

bool SlurSegment::isEdited() const
{
    for (int i = 0; i < int(Grip::GRIPS); ++i) {
        if (!_ups[i].off.isNull()) {
            return true;
        }
    }
    return false;
}

Slur::Slur(const Slur& s)
    : SlurTie(s)
{
    _sourceStemArrangement = s._sourceStemArrangement;
}

//---------------------------------------------------------
//   fixArticulations
//---------------------------------------------------------

static void fixArticulations(PointF& pt, Chord* c, double _up, bool stemSide = false)
{
    //
    // handle special case of tenuto and staccato
    // yo = current offset of slur from chord position
    // return unchanged position, or position of outmost "close" articulation
    //
    for (Articulation* a : c->articulations()) {
        if (!a->layoutCloseToNote() || !a->addToSkyline()) {
            continue;
        }
        // skip if articulation on stem side but slur is not or vice versa
        if ((a->up() == c->up()) != stemSide) {
            continue;
        }
        if (a->isTenuto()) {
            pt.rx() = a->x();
        }
        if (a->up()) {
            pt.ry() = std::min(pt.y(), a->y() + (a->height() + c->score()->spatium() * .3) * _up);
        } else {
            pt.ry() = std::max(pt.y(), a->y() + (a->height() + c->score()->spatium() * .3) * _up);
        }
    }
}

//---------------------------------------------------------
//   slurPos
//    Calculate position of start- and endpoint of slur
//    relative to System() position.
//---------------------------------------------------------

void Slur::slurPosChord(SlurPos* sp)
{
    Chord* stChord;
    Chord* enChord;
    if (startChord()->isGraceAfter()) {      // grace notes after, coming in reverse order
        stChord = endChord();
        enChord = startChord();
        _up = false;
    } else {
        stChord = startChord();
        enChord = endChord();
    }
    Note* _startNote = stChord->downNote();
    Note* _endNote   = enChord->downNote();
    double hw         = _startNote->bboxRightPos();
    double __up       = _up ? -1.0 : 1.0;
    double _spatium = spatium();

    Measure* measure = endChord()->measure();
    sp->system1 = measure->system();
    if (!sp->system1) {               // DEBUG
        LOGD("no system1");
        return;
    }
    assert(sp->system1);
    sp->system2 = sp->system1;
    PointF pp(sp->system1->pagePos());

    double xo;
    double yo;

    //------p1
    if (_up) {
        xo = _startNote->x() + hw * 1.12;
        yo = _startNote->pos().y() + hw * .3 * __up;
    } else {
        xo = _startNote->x() + hw * 0.4;
        yo = _startNote->pos().y() + _spatium * .75 * __up;
    }
    sp->p1 = stChord->pagePos() - pp + PointF(xo, yo);

    //------p2
    if ((enChord->notes().size() > 1) || (enChord->stem() && !enChord->up() && !_up)) {
        xo = _endNote->x() - hw * 0.12;
        yo = _endNote->pos().y() + hw * .3 * __up;
    } else {
        xo = _endNote->x() + hw * 0.15;
        yo = _endNote->pos().y() + _spatium * .75 * __up;
    }
    sp->p2 = enChord->pagePos() - pp + PointF(xo, yo);
}

//---------------------------------------------------------
//   slurPos
//    calculate position of start- and endpoint of slur
//    relative to System() position
//---------------------------------------------------------

void Slur::slurPos(SlurPos* sp)
{
    double _spatium = spatium();
    const double stemSideInset = 0.5;
    const double beamClearance = 0.5;
    const double hookClearanceX = 0.3;
    const double beamAnchorInset = 0.15;
    const double straightStemXOffset = 0.5; // how far down a straight stem a slur attaches (percent)
    // hack alert!! -- fakeCutout
    // The fakeCutout const describes the slope of a line from the top of the stem to the full width of the hook.
    // this is necessary because hooks don't have SMuFL cutouts
    // Gonville and MuseJazz have really weirdly-shaped hooks compared to Leland and Bravura and Emmentaler,
    // so we need to adjust the slope of our hook-avoidance line. this will be unnecessary when hooks have
    // SMuFL anchors
    bool bulkyHook = score()->scoreFont()->family() == "Gonville" || score()->scoreFont()->family() == "MuseJazz";
    const double fakeCutoutSlope = bulkyHook ? 1.5 : 1.0;

    if (endCR() == 0) {
        sp->p1 = startCR()->pagePos();
        sp->p1.rx() += startCR()->width();
        sp->p2 = sp->p1;
        sp->p2.rx() += 5 * _spatium;
        sp->system1 = startCR()->measure()->system();
        sp->system2 = sp->system1;
        return;
    }

    bool useTablature = staff() && staff()->isTabStaff(endCR()->tick());
    bool staffHasStems = true;       // assume staff uses stems
    const StaffType* stt = 0;
    if (useTablature) {
        stt = staff()->staffType(tick());
        staffHasStems = stt->stemThrough();       // if tab with stems beside, stems do not count for slur pos
    }

    // start and end cr, chord, and note
    ChordRest* scr = startCR();
    ChordRest* ecr = endCR();
    Chord* sc = 0;
    Note* note1 = 0;
    if (scr->isChord()) {
        sc = toChord(scr);
        note1 = _up ? sc->upNote() : sc->downNote();
    }
    Chord* ec = 0;
    Note* note2 = 0;
    if (ecr->isChord()) {
        ec = toChord(ecr);
        note2 = _up ? ec->upNote() : ec->downNote();
    }

    sp->system1 = scr->measure()->system();
    sp->system2 = ecr->measure()->system();

    if (sp->system1 == 0) {
        LOGD("no system1");
        return;
    }

    sp->p1 = scr->pos() + scr->segment()->pos() + scr->measure()->pos();
    sp->p2 = ecr->pos() + ecr->segment()->pos() + ecr->measure()->pos();

    // adjust for cross-staff
    if (scr->vStaffIdx() != vStaffIdx() && sp->system1) {
        double diff = sp->system1->staff(scr->vStaffIdx())->y() - sp->system1->staff(vStaffIdx())->y();
        sp->p1.ry() += diff;
    }
    if (ecr->vStaffIdx() != vStaffIdx() && sp->system2) {
        double diff = sp->system2->staff(ecr->vStaffIdx())->y() - sp->system2->staff(vStaffIdx())->y();
        sp->p2.ry() += diff;
    }

    // account for centering or other adjustments (other than mirroring)
    if (note1 && !note1->mirror()) {
        sp->p1.rx() += note1->x();
    }
    if (note2 && !note2->mirror()) {
        sp->p2.rx() += note2->x();
    }

    PointF po = PointF();

    Stem* stem1 = sc && staffHasStems ? sc->stem() : 0;
    Stem* stem2 = ec && staffHasStems ? ec->stem() : 0;

    enum class SlurAnchor : char {
        NONE, STEM
    };
    SlurAnchor sa1 = SlurAnchor::NONE;
    SlurAnchor sa2 = SlurAnchor::NONE;

    if (scr->up() == ecr->up() && scr->up() == _up) {
        if (stem1 && (!scr->beam() || scr->beam()->elements().back() == scr)) {
            sa1 = SlurAnchor::STEM;
        }
        if (stem2 && (!ecr->beam() || ecr->beam()->elements().front() == ecr)) {
            sa2 = SlurAnchor::STEM;
        }
    }

    double __up = _up ? -1.0 : 1.0;
    double hw1 = note1 ? note1->tabHeadWidth(stt) : scr->width();        // if stt == 0, tabHeadWidth()
    double hw2 = note2 ? note2->tabHeadWidth(stt) : ecr->width();        // defaults to headWidth()
    PointF pt;
    switch (sa1) {
    case SlurAnchor::STEM:                //sc can't be null
    {
        // place slur starting point at stem end point
        pt = sc->stemPos() - sc->pagePos() + sc->stem()->p2();
        if (useTablature) {                           // in tabs, stems are centred on note:
            pt.rx() = hw1 * 0.5 + (note1 ? note1->bboxXShift() : 0.0);                      // skip half notehead to touch stem, anatoly-os: incorrect. half notehead width is not always the stem position
        }
        // clear the stem (x)
        // allow slight overlap (y)
        // don't allow overlap with hook if not disabling the autoplace checks against start/end segments in SlurSegment::layoutSegment()
        double yadj = -stemSideInset;
        yadj *= _spatium * __up;
        pt += PointF(0.35 * _spatium, yadj);
        // account for articulations
        fixArticulations(pt, sc, __up, true);
        // adjust for hook
        double fakeCutout = 0.0;
        if (!score()->styleB(Sid::useStraightNoteFlags)) {
            // regular flags
            if (sc->hook() && sc->hook()->bbox().translated(sc->hook()->pos()).contains(pt)) {
                // TODO: in the utopian far future where all hooks have SMuFL cutouts, this fakeCutout business will no
                // longer be used. for the time being fakeCutout describes a point on the line y=mx+b, out from the top of the stem
                // where y = yadj, m = fakeCutoutSlope, and x = y/m + fakeCutout
                fakeCutout = std::min(0.0, std::abs(yadj) - (sc->hook()->width() / fakeCutoutSlope));
                pt.rx() = (sc->hook()->width() + sc->hook()->pos().x() - sc->x() + fakeCutout + (hookClearanceX * _spatium)) * sc->mag();
            }
        } else {
            // straight flags
            if (sc->hook() && sc->hook()->bbox().translated(sc->hook()->pos()).contains(pt)) {
                pt.rx() = (sc->hook()->width() * straightStemXOffset) + sc->hook()->pos().x() - sc->x();
                if (_up) {
                    pt.ry() = sc->downNote()->pos().y() - stem1->height() - (beamClearance * _spatium * .7);
                } else {
                    pt.ry() = sc->upNote()->pos().y() + stem1->height() + (beamClearance * _spatium * .7);
                }
            }
        }
        sp->p1 += pt;
    }
    break;
    case SlurAnchor::NONE:
        break;
    }
    switch (sa2) {
    case SlurAnchor::STEM:                //ec can't be null
    {
        pt = ec->stemPos() - ec->pagePos() + ec->stem()->p2();
        if (useTablature) {
            pt.rx() = hw2 * 0.5;
        }
        // don't allow overlap with beam
        double yadj;
        if (ec->beam() && ec->beam()->elements().front() != ec) {
            yadj = 0.75;
        } else {
            yadj = -stemSideInset;
        }
        yadj *= _spatium * __up;
        pt += PointF(-0.35 * _spatium, yadj);
        // account for articulations
        fixArticulations(pt, ec, __up, true);
        sp->p2 += pt;
    }
    break;
    case SlurAnchor::NONE:
        break;
    }

    //
    // default position:
    //    horizontal: middle of notehead
    //    vertical:   _spatium * .4 above/below notehead
    //
    //------p1
    // Compute x0, y0 and stemPos
    if (sa1 == SlurAnchor::NONE || sa2 == SlurAnchor::NONE) {   // need stemPos if sa2 == SlurAnchor::NONE
        bool stemPos = false;       // p1 starts at chord stem side

        // default positions
        po.rx() = hw1 * .5 + (note1 ? note1->bboxXShift() : 0.0);
        if (note1) {
            po.ry() = note1->pos().y();
        } else if (_up) {
            po.ry() = scr->bbox().top();
        } else {
            po.ry() = scr->bbox().top() + scr->height();
        }
        po.ry() += _spatium * .9 * __up;

        // adjustments for stem and/or beam

        if (stem1) {     //sc not null
            Beam* beam1 = sc->beam();
            if (beam1 && beam1->cross()) {
                // TODO: stem direction is not finalized, so we cannot use it here
                fixArticulations(po, sc, __up, false);
            } else if (beam1 && (beam1->elements().back() != sc) && (sc->up() == _up)) {
                // start chord is beamed but not the last chord of beam group
                // and slur direction is same as start chord (stem side)

                // in these cases, layout start of slur to stem
                double beamWidthSp = score()->styleS(Sid::beamWidth).val() * beam1->mag();
                double sh = stem1->height() + ((beamWidthSp / 2 + beamClearance) * _spatium);
                if (_up) {
                    po.ry() = sc->downNote()->pos().y() - sh;
                } else {
                    po.ry() = sc->upNote()->pos().y() + sh;
                }
                po.rx() = stem1->pos().x() + ((stem1->lineWidthMag() / 2) * __up) + (beamAnchorInset * _spatium);

                // account for articulations
                fixArticulations(po, sc, __up, true);

                // force end of slur to layout to stem as well,
                // if start and end chords have same stem direction
                stemPos = true;
            } else {
                // start chord is not beamed or is last chord of beam group
                // or slur direction is opposite that of start chord

                // at this point slur is in default position relative to note on slur side
                // but we may need to make further adjustments

                // if stem and slur are both up
                // we need to clear stem horizontally
                if (sc->up() && _up) {
                    po.rx() = hw1 + _spatium * .3;
                }

                //
                // handle case: stem up   - stem down
                //              stem down - stem up
                //
                if ((sc->up() != ecr->up()) && (sc->up() == _up)) {
                    // start and end chord have opposite direction
                    // and slur direction is same as start chord
                    // (so slur starts on stem side)

                    // float the start point along the stem to follow direction of movement
                    // see for example Gould p. 111

                    // get position of note on slur side for start & end chords
                    Note* n1  = sc->up() ? sc->upNote() : sc->downNote();
                    Note* n2  = 0;
                    if (ec) {
                        n2 = ec->up() ? ec->upNote() : ec->downNote();
                    }

                    // differential in note positions
                    double yd  = (n2 ? n2->pos().y() : ecr->pos().y()) - n1->pos().y();
                    yd *= .5;

                    // float along stem according to differential
                    double sh = stem1->height();
                    if (_up && yd < 0.0) {
                        po.ry() = std::max(po.y() + yd, sc->downNote()->pos().y() - sh - _spatium);
                    } else if (!_up && yd > 0.0) {
                        po.ry() = std::min(po.y() + yd, sc->upNote()->pos().y() + sh + _spatium);
                    }

                    // account for articulations
                    fixArticulations(po, sc, __up, true);

                    // we may wish to force end to align to stem as well,
                    // if it is in same direction
                    // (but it won't be, so this assignment should have no effect)
                    stemPos = true;
                } else {
                    // avoid articulations
                    fixArticulations(po, sc, __up, sc->up() == _up);
                }
            }
        } else if (sc) {
            // avoid articulations
            fixArticulations(po, sc, __up, sc->up() == _up);
        }

        // TODO: offset start position if there is another slur ending on this cr

        if (sa1 == SlurAnchor::NONE) {
            sp->p1 += po;
        }

        //------p2
        if (sa2 == SlurAnchor::NONE) {
            // default positions
            po.rx() = hw2 * .5 + (note2 ? note2->bboxXShift() : 0.0);
            if (note2) {
                po.ry() = note2->pos().y();
            } else if (_up) {
                po.ry() = endCR()->bbox().top();
            } else {
                po.ry() = endCR()->bbox().top() + endCR()->height();
            }
            po.ry() += _spatium * .9 * __up;

            // adjustments for stem and/or beam

            if (stem2) {       //ec can't be null
                Beam* beam2 = ec->beam();
                if (beam2 && beam2->cross()) {
                    // TODO: stem direction is not finalized, so we cannot use it here
                    fixArticulations(po, ec, __up, false);
                } else if ((stemPos && (scr->up() == ec->up()))
                           || (beam2
                               && (!beam2->elements().empty())
                               && (beam2->elements().front() != ec)
                               && (ec->up() == _up)
                               && sc && (sc->noteType() == NoteType::NORMAL)
                               )
                           ) {
                    // slur start was laid out to stem and start and end have same direction
                    // OR
                    // end chord is beamed but not the first chord of beam group
                    // and slur direction is same as end chord (stem side)
                    // and start chordrest is not a grace chord

                    // in these cases, layout end of slur to stem
                    double beamWidthSp = beam2 ? score()->styleS(Sid::beamWidth).val() * beam2->mag() : 0;
                    double sh = stem2->height() + ((beamClearance + (beamWidthSp / 2)) * _spatium);
                    if (_up) {
                        po.ry() = ec->downNote()->pos().y() - sh;
                    } else {
                        po.ry() = ec->upNote()->pos().y() + sh;
                    }
                    po.rx() = stem2->pos().x() + ((stem2->lineWidthMag() / 2) * __up) - (beamAnchorInset * _spatium);

                    // account for articulations
                    fixArticulations(po, ec, __up, true);
                } else {
                    // slur was not aligned to stem or start and end have different direction
                    // AND
                    // end chord is not beamed or is first chord of beam group
                    // or slur direction is opposite that of end chord

                    // if stem and slur are both down,
                    // we need to clear stem horizontally
                    if (!ec->up() && !_up) {
                        po.rx() = -_spatium * .3 + note2->x();
                    }

                    //
                    // handle case: stem up   - stem down
                    //              stem down - stem up
                    //
                    if ((scr->up() != ec->up()) && (ec->up() == _up)) {
                        // start and end chord have opposite direction
                        // and slur direction is same as end chord
                        // (so slur end on stem side)

                        // float the end point along the stem to follow direction of movement
                        // see for example Gould p. 111

                        Note* n1 = 0;
                        if (sc) {
                            n1 = sc->up() ? sc->upNote() : sc->downNote();
                        }
                        Note* n2 = ec->up() ? ec->upNote() : ec->downNote();

                        double yd = n2->pos().y() - (n1 ? n1->pos().y() : startCR()->pos().y());
                        yd *= .5;

                        double mh = stem2->height();
                        if (_up && yd > 0.0) {
                            po.ry() = std::max(po.y() - yd, ec->downNote()->pos().y() - mh - _spatium);
                        } else if (!_up && yd < 0.0) {
                            po.ry() = std::min(po.y() - yd, ec->upNote()->pos().y() + mh + _spatium);
                        }

                        // account for articulations
                        fixArticulations(po, ec, __up, true);
                    } else {
                        // avoid articulations
                        fixArticulations(po, ec, __up, ec->up() == _up);
                    }
                }
            } else if (ec) {
                // avoid articulations
                fixArticulations(po, ec, __up, ec->up() == _up);
            }
            // TODO: offset start position if there is another slur ending on this cr
            sp->p2 += po;
        }
    }
}

//---------------------------------------------------------
//   Slur
//---------------------------------------------------------

Slur::Slur(EngravingItem* parent)
    : SlurTie(ElementType::SLUR, parent)
{
    setAnchor(Anchor::CHORD);
}

//---------------------------------------------------------
//   calcStemArrangement
//---------------------------------------------------------

int calcStemArrangement(EngravingItem* start, EngravingItem* end)
{
    return (start && toChord(start)->stem() && toChord(start)->stem()->up() ? 2 : 0)
           + (end && end->isChord() && toChord(end)->stem() && toChord(end)->stem()->up() ? 4 : 0);
}

//---------------------------------------------------------
//   write
//---------------------------------------------------------

void Slur::write(XmlWriter& xml) const
{
    if (broken()) {
        LOGD("broken slur not written");
        return;
    }
    if (!xml.context()->canWrite(this)) {
        return;
    }
    xml.startElement(this);
    if (xml.context()->clipboardmode()) {
        xml.tag("stemArr", calcStemArrangement(startElement(), endElement()));
    }
    SlurTie::writeProperties(xml);
    xml.endElement();
}

//---------------------------------------------------------
//   readProperties
//---------------------------------------------------------

bool Slur::readProperties(XmlReader& e)
{
    const AsciiStringView tag(e.name());
    if (tag == "stemArr") {
        _sourceStemArrangement = e.readInt();
        return true;
    }
    return SlurTie::readProperties(e);
}

//---------------------------------------------------------
//   directionMixture
//---------------------------------------------------------

static bool isDirectionMixture(Chord* c1, Chord* c2)
{
    if (c1->track() != c2->track()) {
        return false;
    }
    bool up = c1->up();
    track_idx_t track = c1->track();
    for (Measure* m = c1->measure(); m; m = m->nextMeasure()) {
        for (Segment* seg = m->first(); seg; seg = seg->next(SegmentType::ChordRest)) {
            if (!seg || seg->tick() < c1->tick() || !seg->isChordRestType()) {
                continue;
            }
            if (seg->tick() > c2->tick()) {
                return false;
            }
            EngravingItem* e = seg->element(track);
            if (!e || !e->isChord()) {
                continue;
            }
            Chord* c = toChord(e);
            if (c->up() != up) {
                return true;
            }
        }
    }
    return false;
}

//---------------------------------------------------------
//   layoutSystem
//    layout slurSegment for system
//---------------------------------------------------------

SpannerSegment* Slur::layoutSystem(System* system)
{
    const double horizontalTieClearance = 0.35 * spatium();
    const double tieClearance = 0.65 * spatium();
    const double continuedSlurOffsetY = spatium() * .4;
    const double continuedSlurMaxDiff = 2.5 * spatium();
    Fraction stick = system->firstMeasure()->tick();
    Fraction etick = system->lastMeasure()->endTick();

    SlurSegment* slurSegment = toSlurSegment(getNextLayoutSystemSegment(system, [](System* parent) {
        return new SlurSegment(parent);
    }));

    SpannerSegmentType sst;
    if (tick() >= stick) {
        //
        // this is the first call to layoutSystem,
        // processing the first line segment
        //
        if (track2() == mu::nidx) {
            setTrack2(track());
        }
        if (startCR() == 0 || startCR()->measure() == 0) {
            LOGD("Slur::layout(): track %zu-%zu  %p - %p tick %d-%d null start anchor",
                 track(), track2(), startCR(), endCR(), tick().ticks(), tick2().ticks());
            return slurSegment;
        }
        if (endCR() == 0) {         // sanity check
            setEndElement(startCR());
            setTick2(tick());
        }
        switch (_slurDirection) {
        case DirectionV::UP:
            _up = true;
            break;
        case DirectionV::DOWN:
            _up = false;
            break;
        case DirectionV::AUTO:
        {
            //
            // assumption:
            // slurs have only chords or rests as start/end elements
            //
            if (startCR() == 0 || endCR() == 0) {
                _up = true;
                break;
            }
            Chord* c1 = startCR()->isChord() ? toChord(startCR()) : 0;
            Chord* c2 = endCR()->isChord() ? toChord(endCR()) : 0;

            if (_sourceStemArrangement != -1) {
                if (_sourceStemArrangement != calcStemArrangement(c1, c2)) {
                    // copy & paste from incompatible stem arrangement, so reset bezier points
                    for (int g = 0; g < (int)Grip::GRIPS; ++g) {
                        slurSegment->ups((Grip)g) = UP();
                    }
                }
            }

            if (c1 && c1->beam() && c1->beam()->cross()) {
                // TODO: stem direction is not finalized, so we cannot use it here
                _up = true;
                break;
            }

            _up = !(startCR()->up());

            Measure* m1 = startCR()->measure();

            if (c1 && c2 && !c1->isGrace() && isDirectionMixture(c1, c2)) {
                // slurs go above if there are mixed direction stems between c1 and c2
                // but grace notes are exceptions
                _up = true;
            } else if (m1->hasVoices(startCR()->staffIdx(), tick(), ticks()) && c1 && !c1->isGrace()) {
                // in polyphonic passage, slurs go on the stem side
                _up = startCR()->up();
            }
        }
        break;
        }
        sst = tick2() < etick ? SpannerSegmentType::SINGLE : SpannerSegmentType::BEGIN;
    } else if (tick() < stick && tick2() >= etick) {
        sst = SpannerSegmentType::MIDDLE;
    } else {
        sst = SpannerSegmentType::END;
    }
    slurSegment->setSpannerSegmentType(sst);

    SlurPos sPos;
    slurPos(&sPos);
    PointF p1, p2;
    // adjust for ties
    p1 = sPos.p1;
    p2 = sPos.p2;
    bool constrainLeftAnchor = false;

    // start anchor, either on the start chordrest or at the beginning of the system
    if (sst == SpannerSegmentType::SINGLE || sst == SpannerSegmentType::BEGIN) {
        Chord* sc = startCR()->isChord() ? toChord(startCR()) : nullptr;

        // on chord
        if (sc && sc->notes().size() == 1) {
            Tie* tie = sc->notes()[0]->tieFor();
            PointF endPoint = PointF();
            if (tie && (tie->isInside() || tie->up() != _up)) {
                // there is a tie that starts on this chordrest
                tie = nullptr;
            }
            if (tie) {
                endPoint = tie->segmentAt(0)->ups(Grip::START).pos();
            }
            bool adjustedVertically = false;
            if (tie) {
                if (_up && tie->up()) {
                    if (endPoint.y() - p1.y() < tieClearance) {
                        p1.ry() = endPoint.y() - tieClearance;
                        adjustedVertically = true;
                    }
                } else if (!_up && !tie->up()) {
                    if (p1.y() - endPoint.y() < tieClearance) {
                        p1.ry() = endPoint.y() + tieClearance;
                        adjustedVertically = true;
                    }
                }
            }
            if (!adjustedVertically && sc->notes()[0]->tieBack() && !sc->notes()[0]->tieBack()->isInside()
                && sc->notes()[0]->tieBack()->up() == up()) {
                // there is a tie that ends on this chordrest
                //tie = sc->notes()[0]->tieBack();
                //endPoint = tie->segmentAt(0)->ups(Grip::END).pos();
                p1.rx() += horizontalTieClearance;
            }
        }
    } else if (sst == SpannerSegmentType::END || sst == SpannerSegmentType::MIDDLE) {
        // beginning of system
        ChordRest* firstCr = system->firstChordRest(track());
        double y = p1.y();
        if (firstCr && firstCr == endCR()) {
            constrainLeftAnchor = true;
        }
        if (firstCr && firstCr->isChord()) {
            Chord* chord = toChord(firstCr);
            if (chord) {
                // if both up or both down, deal with avoiding stems and beams
                Note* upNote = chord->upNote();
                Note* downNote = chord->downNote();
                // account for only the stem length that is above the top note (or below the bottom note)
                double stemLength = chord->stem() ? chord->stem()->length() - (downNote->pos().y() - upNote->pos().y()) : 0.0;
                if (_up) {
                    y = chord->upNote()->pos().y() - (chord->upNote()->height() / 2);
                    if (chord->up() && chord->stem() && firstCr != endCR()) {
                        y -= stemLength;
                    }
                } else {
                    y = chord->downNote()->pos().y() + (chord->downNote()->height() / 2);
                    if (!chord->up() && chord->stem() && firstCr != endCR()) {
                        y += stemLength;
                    }
                }
                y += continuedSlurOffsetY * (_up ? -1 : 1);
            }
        }
        p1 = PointF(system->firstNoteRestSegmentX(true), y);

        // adjust for ties at the end of the system
        ChordRest* cr = system->firstChordRest(track());
        if (cr && cr->isChord() && cr->tick() >= stick && cr->tick() <= etick) {
            // TODO: can ties go to or from rests?
            Chord* c = toChord(cr);
            Tie* tie = nullptr;
            PointF endPoint;
            Tie* tieBack = c->notes()[0]->tieBack();
            if (tieBack && !tieBack->isInside() && tieBack->up() == _up) {
                // there is a tie that ends on this chordrest
                tie = tieBack;
                endPoint = tie->backSegment()->ups(Grip::START).pos();
            }
            if (tie) {
                if (_up && tie->up()) {
                    if (endPoint.y() - p1.y() < tieClearance) {
                        p1.ry() = endPoint.y() - tieClearance;
                    }
                } else if (!_up && !tie->up()) {
                    if (p1.y() - endPoint.y() < tieClearance) {
                        p1.ry() = endPoint.y() + tieClearance;
                    }
                }
            }
        }
    }

    // end anchor
    if (sst == SpannerSegmentType::SINGLE || sst == SpannerSegmentType::END) {
        Chord* ec = endCR()->isChord() ? toChord(endCR()) : nullptr;

        // on chord
        if (ec && ec->notes().size() == 1) {
            Tie* tie = ec->notes()[0]->tieBack();
            PointF endPoint;
            if (tie && (tie->isInside() || tie->up() != _up)) {
                tie = nullptr;
            }
            bool adjustedVertically = false;
            if (tie) {
                endPoint = tie->segmentAt(0)->ups(Grip::END).pos();
                if (_up && tie->up()) {
                    if (endPoint.y() - p2.y() < tieClearance) {
                        p2.ry() = endPoint.y() - tieClearance;
                        adjustedVertically = true;
                    }
                } else if (!_up && !tie->up()) {
                    if (p2.y() - endPoint.y() < tieClearance) {
                        p2.ry() = endPoint.y() + tieClearance;
                        adjustedVertically = true;
                    }
                }
            }
            if (!adjustedVertically && ec->notes()[0]->tieFor() && !ec->notes()[0]->tieFor()->isInside()
                && ec->notes()[0]->tieFor()->up() == up()) {
                // there is a tie that starts on this chordrest
                p2.rx() -= horizontalTieClearance;
            }
        }
    } else {
        // at end of system
        ChordRest* lastCr = system->lastChordRest(track());
        double y = p1.y();
        if (lastCr && lastCr == startCR()) {
            y += 0.25 * spatium() * (_up ? -1 : 1);
        } else if (lastCr && lastCr->isChord()) {
            Chord* chord = toChord(lastCr);
            if (chord) {
                Note* upNote = chord->upNote();
                Note* downNote = chord->downNote();
                // account for only the stem length that is above the top note (or below the bottom note)
                double stemLength = chord->stem() ? chord->stem()->length() - (downNote->pos().y() - upNote->pos().y()) : 0.0;
                if (_up) {
                    y = chord->upNote()->pos().y() - (chord->upNote()->height() / 2);
                    if (chord->up() && chord->stem()) {
                        y -= stemLength;
                    }
                } else {
                    y = chord->downNote()->pos().y() + (chord->downNote()->height() / 2);
                    if (!chord->up() && chord->stem()) {
                        y += stemLength;
                    }
                }
                y += continuedSlurOffsetY * (_up ? -1 : 1);
            }
            double diff = _up ? y - p1.y() : p1.y() - y;
            if (diff > continuedSlurMaxDiff) {
                y = p1.y() + (y > p1.y() ? continuedSlurMaxDiff : -continuedSlurMaxDiff);
            }
        }

        p2 = PointF(system->lastNoteRestSegmentX(true), y);

        // adjust for ties at the end of the system
        ChordRest* cr = system->lastChordRest(track());

        if (cr && cr->isChord() && cr->tick() >= stick && cr->tick() <= etick) {
            // TODO: can ties go to or from rests?
            Chord* c = toChord(cr);
            Tie* tie = nullptr;
            PointF endPoint;
            Tie* tieFor = c->notes()[0]->tieFor();
            if (tieFor && !tieFor->isInside() && tieFor->up() == up()) {
                // there is a tie that starts on this chordrest
                tie = tieFor;
                endPoint = tie->segmentAt(0)->ups(Grip::END).pos();
            }
            if (tie) {
                if (_up && tie->up()) {
                    if (endPoint.y() - p2.y() < tieClearance) {
                        p2.ry() = endPoint.y() - tieClearance;
                    }
                } else if (!_up && !tie->up()) {
                    if (p2.y() - endPoint.y() < tieClearance) {
                        p2.ry() = endPoint.y() + tieClearance;
                    }
                }
            }
        }
    }
    if (constrainLeftAnchor) {
        p1.ry() = p2.y() + (0.25 * spatium() * (_up ? -1 : 1));
    }

    slurSegment->layoutSegment(p1, p2);
    return slurSegment;
}

//---------------------------------------------------------
//   layout
//---------------------------------------------------------

void Slur::layout()
{
    if (track2() == mu::nidx) {
        setTrack2(track());
    }

    double _spatium = spatium();

    if (score()->isPaletteScore() || tick() == Fraction(-1, 1)) {
        //
        // when used in a palette, slur has no parent and
        // tick and tick2 has no meaning so no layout is
        // possible and needed
        //
        SlurSegment* s;
        if (spannerSegments().empty()) {
            s = new SlurSegment(score()->dummy()->system());
            s->setTrack(track());
            add(s);
        } else {
            s = frontSegment();
        }
        s->setSpannerSegmentType(SpannerSegmentType::SINGLE);
        s->layoutSegment(PointF(0, 0), PointF(_spatium * 6, 0));
        setbbox(frontSegment()->bbox());
        return;
    }

    if (startCR() == 0 || startCR()->measure() == 0) {
        LOGD("track %zu-%zu  %p - %p tick %d-%d null start anchor",
             track(), track2(), startCR(), endCR(), tick().ticks(), tick2().ticks());
        return;
    }
    if (endCR() == 0) {       // sanity check
        LOGD("no end CR for %d", (tick() + ticks()).ticks());
        setEndElement(startCR());
        setTick2(tick());
    }
    switch (_slurDirection) {
    case DirectionV::UP:
        _up = true;
        break;
    case DirectionV::DOWN:
        _up = false;
        break;
    case DirectionV::AUTO:
    {
        //
        // assumption:
        // slurs have only chords or rests as start/end elements
        //
        if (startCR() == 0 || endCR() == 0) {
            _up = true;
            break;
        }
        Measure* m1 = startCR()->measure();

        Chord* c1 = startCR()->isChord() ? toChord(startCR()) : 0;
        Chord* c2 = endCR()->isChord() ? toChord(endCR()) : 0;

        _up = !(startCR()->up());

        if ((endCR()->tick() - startCR()->tick()) > m1->ticks()) {
            // long slurs are always above
            _up = true;
        } else {
            _up = !(startCR()->up());
        }

        if (c1 && c2 && isDirectionMixture(c1, c2) && (c1->noteType() == NoteType::NORMAL)) {
            // slurs go above if start and end note have different stem directions,
            // but grace notes are exceptions
            _up = true;
        } else if (m1->hasVoices(startCR()->staffIdx(), tick(), ticks()) && c1 && c1->noteType() == NoteType::NORMAL) {
            // in polyphonic passage, slurs go on the stem side
            _up = startCR()->up();
        }
    }
    break;
    }

    SlurPos sPos;
    slurPos(&sPos);

    const std::vector<System*>& sl = score()->systems();
    ciSystem is = sl.begin();
    while (is != sl.end()) {
        if (*is == sPos.system1) {
            break;
        }
        ++is;
    }
    if (is == sl.end()) {
        LOGD("Slur::layout  first system not found");
    }
    setPos(0, 0);

    //---------------------------------------------------------
    //   count number of segments, if no change, all
    //    user offsets (drags) are retained
    //---------------------------------------------------------

    unsigned nsegs = 1;
    for (ciSystem iis = is; iis != sl.end(); ++iis) {
        if ((*iis)->vbox()) {
            continue;
        }
        if (*iis == sPos.system2) {
            break;
        }
        ++nsegs;
    }

    fixupSegments(nsegs);

    for (int i = 0; is != sl.end(); ++i, ++is) {
        System* system  = *is;
        if (system->vbox()) {
            --i;
            continue;
        }
        SlurSegment* segment = segmentAt(i);
        segment->setSystem(system);

        // case 1: one segment
        if (sPos.system1 == sPos.system2) {
            segment->setSpannerSegmentType(SpannerSegmentType::SINGLE);
            segment->layoutSegment(sPos.p1, sPos.p2);
        }
        // case 2: start segment
        else if (i == 0) {
            segment->setSpannerSegmentType(SpannerSegmentType::BEGIN);
            double x = system->bbox().width();
            segment->layoutSegment(sPos.p1, PointF(x, sPos.p1.y()));
        }
        // case 3: middle segment
        else if (i != 0 && system != sPos.system2) {
            segment->setSpannerSegmentType(SpannerSegmentType::MIDDLE);
            double x1 = system->firstNoteRestSegmentX(true);
            double x2 = system->bbox().width();
            double y  = staffIdx() > system->staves().size() ? system->y() : system->staff(staffIdx())->y();
            segment->layoutSegment(PointF(x1, y), PointF(x2, y));
        }
        // case 4: end segment
        else {
            segment->setSpannerSegmentType(SpannerSegmentType::END);
            double x = system->firstNoteRestSegmentX(true);
            segment->layoutSegment(PointF(x, sPos.p2.y()), sPos.p2);
        }
        if (system == sPos.system2) {
            break;
        }
    }
    setbbox(spannerSegments().empty() ? RectF() : frontSegment()->bbox());
}

//---------------------------------------------------------
//   setTrack
//---------------------------------------------------------

void Slur::setTrack(track_idx_t n)
{
    EngravingItem::setTrack(n);
    for (SpannerSegment* ss : spannerSegments()) {
        ss->setTrack(n);
    }
}
}
