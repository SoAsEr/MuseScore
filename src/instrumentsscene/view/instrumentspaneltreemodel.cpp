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
#include "instrumentspaneltreemodel.h"

#include <algorithm>

#include "translation.h"
#include "roottreeitem.h"
#include "parttreeitem.h"
#include "stafftreeitem.h"
#include "staffcontroltreeitem.h"
#include "log.h"

#include "uicomponents/view/itemmultiselectionmodel.h"

using namespace mu::instrumentsscene;
using namespace mu::notation;
using namespace mu::uicomponents;
using ItemType = InstrumentsTreeItemType::ItemType;

namespace mu::instrumentsscene {
static QString notationToKey(const INotationPtr notation)
{
    std::stringstream stream;
    stream << notation.get();

    return QString::fromStdString(stream.str());
}
}

InstrumentsPanelTreeModel::InstrumentsPanelTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    m_partsNotifyReceiver = std::make_shared<async::Asyncable>();

    m_selectionModel = new ItemMultiSelectionModel(this);
    m_selectionModel->setAllowedModifiers(Qt::ShiftModifier);

    connect(m_selectionModel, &ItemMultiSelectionModel::selectionChanged,
            [this](const QItemSelection& selected, const QItemSelection& deselected) {
        setItemsSelected(deselected.indexes(), false);
        setItemsSelected(selected.indexes(), true);

        updateRearrangementAvailability();
        updateRemovingAvailability();
    });

    onMasterNotationChanged();
    context()->currentMasterNotationChanged().onNotify(this, [this]() {
        onMasterNotationChanged();
    });

    onNotationChanged();
    context()->currentNotationChanged().onNotify(this, [this]() {
        onNotationChanged();
    });
}

void InstrumentsPanelTreeModel::onMasterNotationChanged()
{
    m_masterNotation = context()->currentMasterNotation();
    initPartOrders();
}

void InstrumentsPanelTreeModel::onNotationChanged()
{
    m_partsNotifyReceiver->disconnectAll();

    onBeforeChangeNotation();
    m_notation = context()->currentNotation();

    if (m_notation) {
        load();
    } else {
        clear();
    }
}

InstrumentsPanelTreeModel::~InstrumentsPanelTreeModel()
{
    deleteItems();
}

bool InstrumentsPanelTreeModel::removeRows(int row, int count, const QModelIndex& parent)
{
    AbstractInstrumentsPanelTreeItem* parentItem = modelIndexToItem(parent);

    if (!parentItem) {
        parentItem = m_rootItem;
    }

    m_isLoadingBlocked = true;
    beginRemoveRows(parent, row, row + count - 1);

    parentItem->removeChildren(row, count, true);

    endRemoveRows();
    m_isLoadingBlocked = false;

    emit isEmptyChanged();

    return true;
}

void InstrumentsPanelTreeModel::initPartOrders()
{
    m_sortedPartIdList.clear();

    if (!m_masterNotation) {
        return;
    }

    for (IExcerptNotationPtr excerpt : m_masterNotation->excerpts().val) {
        NotationKey key = notationToKey(excerpt->notation());

        for (const Part* part : excerpt->notation()->parts()->partList()) {
            m_sortedPartIdList[key] << part->id();
        }
    }
}

void InstrumentsPanelTreeModel::onBeforeChangeNotation()
{
    if (!m_notation || !m_rootItem) {
        return;
    }

    QList<ID> partIdList;

    for (int i = 0; i < m_rootItem->childCount(); ++i) {
        partIdList << m_rootItem->childAtRow(i)->id();
    }

    m_sortedPartIdList[notationToKey(m_notation)] = partIdList;
}

void InstrumentsPanelTreeModel::setupPartsConnections()
{
    async::NotifyList<const Part*> notationParts = m_notation->parts()->partList();

    notationParts.onChanged(m_partsNotifyReceiver.get(), [this]() {
        load();
    });

    auto updateMasterPartItem = [this](const ID& partId) {
        auto partItem = dynamic_cast<PartTreeItem*>(m_rootItem->childAtId(partId));
        if (!partItem) {
            return;
        }

        partItem->init(m_masterNotation->parts()->part(partId));
        updateRemovingAvailability();
    };

    notationParts.onItemAdded(m_partsNotifyReceiver.get(), [updateMasterPartItem](const Part* part) {
        updateMasterPartItem(part->id());
    });

    notationParts.onItemChanged(m_partsNotifyReceiver.get(), [updateMasterPartItem](const Part* part) {
        updateMasterPartItem(part->id());
    });
}

void InstrumentsPanelTreeModel::setupStavesConnections(const ID& stavesPartId)
{
    async::NotifyList<const Staff*> notationStaves = m_notation->parts()->staffList(stavesPartId);

    notationStaves.onItemChanged(m_partsNotifyReceiver.get(), [this, stavesPartId](const Staff* staff) {
        auto partItem = m_rootItem->childAtId(stavesPartId);
        if (!partItem) {
            return;
        }

        auto staffItem = dynamic_cast<StaffTreeItem*>(partItem->childAtId(staff->id()));
        if (!staffItem) {
            return;
        }

        staffItem->init(m_masterNotation->parts()->staff(staff->id()));
    });

    notationStaves.onItemAdded(m_partsNotifyReceiver.get(), [this, stavesPartId](const Staff* staff) {
        auto partItem = m_rootItem->childAtId(stavesPartId);
        if (!partItem) {
            return;
        }

        const Staff* masterStaff = m_masterNotation->parts()->staff(staff->id());
        auto staffItem = buildMasterStaffItem(masterStaff, partItem);

        QModelIndex partIndex = index(partItem->row(), 0, QModelIndex());

        beginInsertRows(partIndex, partItem->childCount() - 1, partItem->childCount() - 1);
        partItem->insertChild(staffItem, partItem->childCount() - 1);
        endInsertRows();
    });
}

void InstrumentsPanelTreeModel::listenNotationSelectionChanged()
{
    m_notation->interaction()->selectionChanged().onNotify(this, [this]() {
        std::vector<EngravingItem*> selectedElements = m_notation->interaction()->selection()->elements();

        if (selectedElements.empty()) {
            m_selectionModel->clear();
            return;
        }

        QSet<ID> selectedPartIdSet;
        for (const EngravingItem* element : selectedElements) {
            if (!element->part()) {
                continue;
            }

            selectedPartIdSet << element->part()->id();
        }

        for (const ID& selectedPartId : selectedPartIdSet) {
            AbstractInstrumentsPanelTreeItem* item = m_rootItem->childAtId(selectedPartId);

            if (item) {
                m_selectionModel->select(createIndex(item->row(), 0, item));
            }
        }
    });
}

void InstrumentsPanelTreeModel::clear()
{
    TRACEFUNC;

    beginResetModel();
    deleteItems();
    endResetModel();

    emit isEmptyChanged();
    emit isAddingAvailableChanged(false);
}

void InstrumentsPanelTreeModel::deleteItems()
{
    m_selectionModel->clear();
    delete m_rootItem;
    m_rootItem = nullptr;
}

void InstrumentsPanelTreeModel::load()
{
    if (m_isLoadingBlocked) {
        return;
    }

    TRACEFUNC;

    beginResetModel();
    deleteItems();

    m_rootItem = new RootTreeItem(m_masterNotation, m_notation);

    async::NotifyList<const Part*> masterParts = m_masterNotation->parts()->partList();
    sortParts(masterParts);

    for (const Part* part : masterParts) {
        m_rootItem->appendChild(loadMasterPart(part));
    }

    endResetModel();

    setupPartsConnections();
    listenNotationSelectionChanged();

    emit isEmptyChanged();
    emit isAddingAvailableChanged(true);
}

void InstrumentsPanelTreeModel::sortParts(notation::PartList& parts)
{
    NotationKey key = notationToKey(m_notation);

    if (!m_sortedPartIdList.contains(key)) {
        return;
    }

    const QList<ID>& sortedPartIdList = m_sortedPartIdList[key];

    std::sort(parts.begin(), parts.end(), [&sortedPartIdList](const Part* part1, const Part* part2) {
        int index1 = sortedPartIdList.indexOf(part1->id());
        int index2 = sortedPartIdList.indexOf(part2->id());

        if (index1 < 0) {
            index1 = std::numeric_limits<int>::max();
        }

        if (index2 < 0) {
            index2 = std::numeric_limits<int>::max();
        }

        return index1 < index2;
    });
}

void InstrumentsPanelTreeModel::selectRow(const QModelIndex& rowIndex)
{
    m_selectionModel->select(rowIndex);
}

void InstrumentsPanelTreeModel::clearSelection()
{
    m_selectionModel->clear();
}

void InstrumentsPanelTreeModel::addInstruments()
{
    dispatcher()->dispatch("instruments");
}

void InstrumentsPanelTreeModel::moveSelectedRowsUp()
{
    QModelIndexList selectedIndexList = m_selectionModel->selectedIndexes();

    if (selectedIndexList.isEmpty()) {
        return;
    }

    std::sort(selectedIndexList.begin(), selectedIndexList.end(), [](QModelIndex f, QModelIndex s) -> bool {
        return f.row() < s.row();
    });

    QModelIndex sourceRowFirst = selectedIndexList.first();

    moveRows(sourceRowFirst.parent(), sourceRowFirst.row(), selectedIndexList.count(), sourceRowFirst.parent(), sourceRowFirst.row() - 1);
}

void InstrumentsPanelTreeModel::moveSelectedRowsDown()
{
    QModelIndexList selectedIndexList = m_selectionModel->selectedIndexes();

    if (selectedIndexList.isEmpty()) {
        return;
    }

    std::sort(selectedIndexList.begin(), selectedIndexList.end(), [](QModelIndex f, QModelIndex s) -> bool {
        return f.row() < s.row();
    });

    QModelIndex sourceRowFirst = selectedIndexList.first();
    QModelIndex sourceRowLast = selectedIndexList.last();

    moveRows(sourceRowFirst.parent(), sourceRowFirst.row(), selectedIndexList.count(), sourceRowFirst.parent(), sourceRowLast.row() + 1);
}

void InstrumentsPanelTreeModel::removeSelectedRows()
{
    if (!m_selectionModel) {
        return;
    }

    QModelIndexList selectedIndexList = m_selectionModel->selectedIndexes();
    if (selectedIndexList.empty()) {
        return;
    }

    QModelIndex firstIndex = *std::min_element(selectedIndexList.cbegin(), selectedIndexList.cend(),
                                               [](const QModelIndex& f, const QModelIndex& s) {
        return f.row() < s.row();
    });

    removeRows(firstIndex.row(), selectedIndexList.size(), firstIndex.parent());
}

bool InstrumentsPanelTreeModel::moveRows(const QModelIndex& sourceParent, int sourceRow, int count, const QModelIndex& destinationParent,
                                         int destinationChild)
{
    m_isLoadingBlocked = true;

    AbstractInstrumentsPanelTreeItem* sourceParentItem = modelIndexToItem(sourceParent);
    AbstractInstrumentsPanelTreeItem* destinationParentItem = modelIndexToItem(destinationParent);

    if (!sourceParentItem) {
        sourceParentItem = m_rootItem;
    }

    if (!destinationParentItem) {
        destinationParentItem = m_rootItem;
    }

    int sourceFirstRow = sourceRow;
    int sourceLastRow = sourceRow + count - 1;
    int destinationRow = (sourceLastRow > destinationChild
                          || sourceParentItem != destinationParentItem) ? destinationChild : destinationChild + 1;

    beginMoveRows(sourceParent, sourceFirstRow, sourceLastRow, destinationParent, destinationRow);
    sourceParentItem->moveChildren(sourceFirstRow, count, destinationParentItem, destinationRow);
    endMoveRows();

    updateRearrangementAvailability();

    m_isLoadingBlocked = false;

    return true;
}

void InstrumentsPanelTreeModel::toggleVisibilityOfSelectedRows(bool visible)
{
    if (!m_selectionModel || !m_selectionModel->hasSelection()) {
        return;
    }

    QModelIndexList selectedIndexes = m_selectionModel->selectedIndexes();

    for (QModelIndex index : selectedIndexes) {
        AbstractInstrumentsPanelTreeItem* item = modelIndexToItem(index);

        item->setIsVisible(visible);
    }
}

QItemSelectionModel* InstrumentsPanelTreeModel::selectionModel() const
{
    return m_selectionModel;
}

QModelIndex InstrumentsPanelTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }

    AbstractInstrumentsPanelTreeItem* parentItem = nullptr;

    if (!parent.isValid()) {
        parentItem = m_rootItem;
    } else {
        parentItem = modelIndexToItem(parent);
    }

    if (!parentItem) {
        return QModelIndex();
    }

    AbstractInstrumentsPanelTreeItem* childItem = parentItem->childAtRow(row);

    if (childItem) {
        return createIndex(row, column, childItem);
    }

    return QModelIndex();
}

QModelIndex InstrumentsPanelTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid()) {
        return QModelIndex();
    }

    AbstractInstrumentsPanelTreeItem* childItem = modelIndexToItem(child);
    AbstractInstrumentsPanelTreeItem* parentItem = qobject_cast<AbstractInstrumentsPanelTreeItem*>(childItem->parentItem());

    if (parentItem == m_rootItem) {
        return QModelIndex();
    }

    return createIndex(parentItem->row(), 0, parentItem);
}

int InstrumentsPanelTreeModel::rowCount(const QModelIndex& parent) const
{
    const AbstractInstrumentsPanelTreeItem* parentItem = m_rootItem;

    if (parent.isValid()) {
        parentItem = modelIndexToItem(parent);
    }

    return parentItem ? parentItem->childCount() : 0;
}

int InstrumentsPanelTreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant InstrumentsPanelTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() && role != ItemRole) {
        return QVariant();
    }

    AbstractInstrumentsPanelTreeItem* item = modelIndexToItem(index);
    return item ? QVariant::fromValue(qobject_cast<QObject*>(item)) : QVariant();
}

QHash<int, QByteArray> InstrumentsPanelTreeModel::roleNames() const
{
    return { { ItemRole, "itemRole" } };
}

void InstrumentsPanelTreeModel::setIsMovingUpAvailable(bool isMovingUpAvailable)
{
    if (m_isMovingUpAvailable == isMovingUpAvailable) {
        return;
    }

    m_isMovingUpAvailable = isMovingUpAvailable;
    emit isMovingUpAvailableChanged(m_isMovingUpAvailable);
}

void InstrumentsPanelTreeModel::setIsMovingDownAvailable(bool isMovingDownAvailable)
{
    if (m_isMovingDownAvailable == isMovingDownAvailable) {
        return;
    }

    m_isMovingDownAvailable = isMovingDownAvailable;
    emit isMovingDownAvailableChanged(m_isMovingDownAvailable);
}

bool InstrumentsPanelTreeModel::isMovingUpAvailable() const
{
    return m_isMovingUpAvailable;
}

bool InstrumentsPanelTreeModel::isMovingDownAvailable() const
{
    return m_isMovingDownAvailable;
}

bool InstrumentsPanelTreeModel::isRemovingAvailable() const
{
    return m_isRemovingAvailable;
}

bool InstrumentsPanelTreeModel::isAddingAvailable() const
{
    return m_notation != nullptr;
}

bool InstrumentsPanelTreeModel::isEmpty() const
{
    return m_rootItem ? m_rootItem->isEmpty() : true;
}

void InstrumentsPanelTreeModel::setIsRemovingAvailable(bool isRemovingAvailable)
{
    if (m_isRemovingAvailable == isRemovingAvailable) {
        return;
    }

    m_isRemovingAvailable = isRemovingAvailable;
    emit isRemovingAvailableChanged(m_isRemovingAvailable);
}

void InstrumentsPanelTreeModel::updateRearrangementAvailability()
{
    QModelIndexList selectedIndexList = m_selectionModel->selectedIndexes();

    if (selectedIndexList.isEmpty()) {
        updateMovingUpAvailability(false);
        updateMovingDownAvailability(false);
        return;
    }

    std::sort(selectedIndexList.begin(), selectedIndexList.end(), [](QModelIndex f, QModelIndex s) -> bool {
        return f.row() < s.row();
    });

    bool isRearrangementAvailable = true;

    QMutableListIterator<QModelIndex> it(selectedIndexList);

    while (it.hasNext() && selectedIndexList.count() > 1) {
        int nextRow = it.next().row();
        int previousRow = it.peekPrevious().row();

        isRearrangementAvailable = (nextRow - previousRow <= 1);

        if (!isRearrangementAvailable) {
            updateMovingUpAvailability(isRearrangementAvailable);
            updateMovingDownAvailability(isRearrangementAvailable);
            return;
        }
    }

    updateMovingUpAvailability(isRearrangementAvailable, selectedIndexList.first());
    updateMovingDownAvailability(isRearrangementAvailable, selectedIndexList.last());
}

void InstrumentsPanelTreeModel::updateMovingUpAvailability(bool isSelectionMovable, const QModelIndex& firstSelectedRowIndex)
{
    bool isRowInBoundaries = firstSelectedRowIndex.isValid() ? firstSelectedRowIndex.row() > 0 : false;

    setIsMovingUpAvailable(isSelectionMovable && isRowInBoundaries);
}

void InstrumentsPanelTreeModel::updateMovingDownAvailability(bool isSelectionMovable, const QModelIndex& lastSelectedRowIndex)
{
    AbstractInstrumentsPanelTreeItem* parentItem = modelIndexToItem(lastSelectedRowIndex.parent());
    if (!parentItem) {
        parentItem = m_rootItem;
    }

    // exclude the control item
    bool hasControlItem = static_cast<ItemType>(parentItem->type()) != ItemType::ROOT;
    int lastItemRowIndex = parentItem->childCount() - 1 - (hasControlItem ? 1 : 0);

    bool isRowInBoundaries = lastSelectedRowIndex.isValid() ? lastSelectedRowIndex.row() < lastItemRowIndex : false;

    setIsMovingDownAvailable(isSelectionMovable && isRowInBoundaries);
}

void InstrumentsPanelTreeModel::updateRemovingAvailability()
{
    bool isRemovingAvailable = m_selectionModel->hasSelection();

    for (const QModelIndex& index : m_selectionModel->selectedIndexes()) {
        const AbstractInstrumentsPanelTreeItem* item = modelIndexToItem(index);
        isRemovingAvailable &= (item && item->isRemovable());
    }

    setIsRemovingAvailable(isRemovingAvailable);
}

void InstrumentsPanelTreeModel::setItemsSelected(const QModelIndexList& indexes, bool selected)
{
    for (const QModelIndex& index : indexes) {
        if (AbstractInstrumentsPanelTreeItem* item = modelIndexToItem(index)) {
            item->setIsSelected(selected);
        }
    }
}

AbstractInstrumentsPanelTreeItem* InstrumentsPanelTreeModel::loadMasterPart(const Part* masterPart)
{
    TRACEFUNC;

    auto partItem = buildPartItem(masterPart);

    for (const Staff* staff : m_masterNotation->parts()->staffList(partItem->id())) {
        auto staffItem = buildMasterStaffItem(staff, partItem);
        partItem->appendChild(staffItem);
    }

    auto addStaffControlItem = buildAddStaffControlItem(partItem->id(), partItem);
    partItem->appendChild(addStaffControlItem);

    setupStavesConnections(partItem->id());

    return partItem;
}

AbstractInstrumentsPanelTreeItem* InstrumentsPanelTreeModel::buildPartItem(const Part* masterPart)
{
    auto result = new PartTreeItem(m_masterNotation, m_notation, m_rootItem);
    result->init(masterPart);

    return result;
}

AbstractInstrumentsPanelTreeItem* InstrumentsPanelTreeModel::buildMasterStaffItem(const Staff* masterStaff, QObject* parent)
{
    auto result = new StaffTreeItem(m_masterNotation, m_notation, parent);
    result->init(masterStaff);

    return result;
}

AbstractInstrumentsPanelTreeItem* InstrumentsPanelTreeModel::buildAddStaffControlItem(const ID& partId, QObject* parent)
{
    auto result = new StaffControlTreeItem(m_masterNotation, m_notation, parent);
    result->init(partId);

    return result;
}

AbstractInstrumentsPanelTreeItem* InstrumentsPanelTreeModel::modelIndexToItem(const QModelIndex& index) const
{
    return static_cast<AbstractInstrumentsPanelTreeItem*>(index.internalPointer());
}
