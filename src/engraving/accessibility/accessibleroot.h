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
#ifndef MU_ENGRAVING_ACCESSIBLEROOT_H
#define MU_ENGRAVING_ACCESSIBLEROOT_H

#include "accessibleitem.h"
#include "../libmscore/rootitem.h"

namespace mu::engraving {
using AccessibleMapToScreenFunc = std::function<RectF(const RectF&)>;

class AccessibleRoot : public AccessibleItem
{
public:
    AccessibleRoot(RootItem* e);
    ~AccessibleRoot() override;

    void setFocusedElement(AccessibleItemPtr e);
    AccessibleItemWeakPtr focusedElement() const;

    void setMapToScreenFunc(const AccessibleMapToScreenFunc& func);
    RectF toScreenRect(const RectF& rect, bool* ok = nullptr) const;

    const accessibility::IAccessible* accessibleParent() const override;
    accessibility::IAccessible::Role accessibleRole() const override;
    QString accessibleName() const override;

    bool enabled() const;
    void setEnabled(bool enabled);

    QString staffInfo() const;

private:
    void updateStaffInfo(const AccessibleItemWeakPtr newAccessibleItem, const AccessibleItemWeakPtr oldAccessibleItem);

    bool m_enabled = false;

    AccessibleItemWeakPtr m_focusedElement;

    AccessibleMapToScreenFunc m_accessibleMapToScreenFunc;

    QString m_staffInfo;
};
}

#endif // MU_ENGRAVING_ACCESSIBLEROOT_H
