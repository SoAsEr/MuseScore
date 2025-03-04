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

#include "masternotationparts.h"

#include "libmscore/masterscore.h"
#include "libmscore/scoreorder.h"
#include "libmscore/undo.h"

#include "log.h"

using namespace mu::notation;

MasterNotationParts::MasterNotationParts(IGetScore* getScore, INotationInteractionPtr interaction, INotationUndoStackPtr undoStack)
    : NotationParts(getScore, interaction, undoStack)
{
}

void MasterNotationParts::setExcerpts(ExcerptNotationList excerpts)
{
    m_excerpts = excerpts;
}

void MasterNotationParts::startGlobalEdit()
{
    NotationParts::startEdit();
    undoStack()->lock();
}

void MasterNotationParts::endGlobalEdit()
{
    undoStack()->unlock();
    NotationParts::apply();
}

void MasterNotationParts::removeParts(const IDList& partsIds)
{
    TRACEFUNC;

    startGlobalEdit();

    NotationParts::removeParts(partsIds);

    for (INotationPartsPtr parts : excerptsParts()) {
        parts->removeParts(partsIds);
    }

    endGlobalEdit();
}

void MasterNotationParts::removeStaves(const IDList& stavesIds)
{
    TRACEFUNC;

    startGlobalEdit();

    NotationParts::removeStaves(stavesIds);

    for (INotationPartsPtr parts : excerptsParts()) {
        parts->removeStaves(stavesIds);
    }

    endGlobalEdit();
}

void MasterNotationParts::insertStaff(Staff* staff, const ID& destinationPartId, size_t index)
{
    TRACEFUNC;

    startGlobalEdit();

    //! NOTE: will be generated later after adding to the score
    staff->setId(mu::engraving::INVALID_ID);

    NotationParts::insertStaff(staff, destinationPartId, index);

    size_t firstStaffInPartIndex = 0;
    for (const Part* part : partList()) {
        if (part->id() == destinationPartId) {
            break;
        }
        firstStaffInPartIndex += part->nstaves();
    }

    for (INotationPartsPtr parts : excerptsParts()) {
        Staff* excerptStaff = mu::engraving::toStaff(staff->linkedClone());
        parts->insertStaff(excerptStaff, destinationPartId, index - firstStaffInPartIndex);
    }

    endGlobalEdit();
}

void MasterNotationParts::appendLinkedStaff(Staff* staff, const mu::ID& sourceStaffId, const mu::ID& destinationPartId)
{
    TRACEFUNC;

    startGlobalEdit();

    //! NOTE: will be generated later after adding to the score
    staff->setId(mu::engraving::INVALID_ID);

    NotationParts::appendLinkedStaff(staff, sourceStaffId, destinationPartId);

    for (INotationPartsPtr parts : excerptsParts()) {
        parts->appendLinkedStaff(staff->clone(), sourceStaffId, destinationPartId);
    }

    endGlobalEdit();
}

void MasterNotationParts::replaceInstrument(const InstrumentKey& instrumentKey, const Instrument& newInstrument)
{
    TRACEFUNC;

    startGlobalEdit();

    NotationParts::replaceInstrument(instrumentKey, newInstrument);

    for (INotationPartsPtr parts : excerptsParts()) {
        parts->replaceInstrument(instrumentKey, newInstrument);
    }

    endGlobalEdit();
}

void MasterNotationParts::replaceDrumset(const InstrumentKey& instrumentKey, const Drumset& newDrumset)
{
    TRACEFUNC;

    startGlobalEdit();

    NotationParts::replaceDrumset(instrumentKey, newDrumset);

    for (INotationPartsPtr parts : excerptsParts()) {
        parts->replaceDrumset(instrumentKey, newDrumset);
    }

    endGlobalEdit();
}

std::vector<INotationPartsPtr> MasterNotationParts::excerptsParts() const
{
    std::vector<INotationPartsPtr> result;

    for (IExcerptNotationPtr excerpt : m_excerpts) {
        result.push_back(excerpt->notation()->parts());
    }

    return result;
}
