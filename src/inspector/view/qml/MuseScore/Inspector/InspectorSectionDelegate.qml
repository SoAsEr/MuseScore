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
import QtQuick 2.15

import MuseScore.Ui 1.0
import MuseScore.UiComponents 1.0
import MuseScore.Inspector 1.0

import "common"
import "general"
import "measures"
import "notation"
import "text"
import "score"

ExpandableBlank {
    id: root

    property int index: -1
    property var sectionModel // Comes from inspectorListModel
    property var anchorItem: null
    property var navigationSection: null

    property var notationView: null

    signal returnToBoundsRequested()
    signal ensureContentVisibleRequested(int invisibleContentHeight)
    signal popupOpened(var openedPopup)

    NavigationPanel {
        id: navPanel
        name: root.title
        section: root.navigationSection
        direction: NavigationPanel.Vertical
        accessible.name: root.title
        enabled: root.enabled && root.visible
        order: root.index + 2
    }

    navigation.panel: navPanel
    navigation.row: 0

    title: root.sectionModel ? root.sectionModel.title : ""

    contentItemComponent: {
        if (!root.sectionModel) {
            return undefined
        }

        switch (root.sectionModel.sectionType) {
        case Inspector.SECTION_GENERAL: return generalSection
        case Inspector.SECTION_MEASURES: return measuresSection
        case Inspector.SECTION_TEXT: return textSection
        case Inspector.SECTION_NOTATION:
            if (sectionModel.isMultiModel) {
                return notationMultiElementsSection
            } else {
                return notationSingleElementSection
            }
        case Inspector.SECTION_SCORE_DISPLAY: return scoreSection
        case Inspector.SECTION_SCORE_APPEARANCE: return scoreAppearanceSection
        }

        return undefined
    }

    onContentItemComponentChanged: {
        root.returnToBoundsRequested()
    }

    Component {
        id: generalSection

        GeneralInspectorView {
            model: root.sectionModel
            navigationPanel: navPanel
            navigationRowStart: root.navigation.row + 1
            anchorItem: root.anchorItem

            notationView: root.notationView

            onEnsureContentVisibleRequested: function(invisibleContentHeight) {
                root.ensureContentVisibleRequested(-invisibleContentHeight)
            }

            onPopupOpened: {
                root.popupOpened(openedPopup)
            }
        }
    }

    Component {
        id: measuresSection

        MeasuresInspectorView {
            model: root.sectionModel
            navigationPanel: navPanel
            navigationRowStart: root.navigation.row + 1
            anchorItem: root.anchorItem

            notationView: root.notationView

            onEnsureContentVisibleRequested: function(invisibleContentHeight) {
                root.ensureContentVisibleRequested(-invisibleContentHeight)
            }

            onPopupOpened: {
                root.popupOpened(openedPopup)
            }
        }
    }

    Component {
        id: textSection

        TextInspectorView {
            model: root.sectionModel
            navigationPanel: navPanel
            navigationRowStart: root.navigation.row + 1
            anchorItem: root.anchorItem

            notationView: root.notationView

            onEnsureContentVisibleRequested: function(invisibleContentHeight) {
                root.ensureContentVisibleRequested(-invisibleContentHeight)
            }

            onPopupOpened: {
                root.popupOpened(openedPopup)
            }
        }
    }

    Component {
        id: notationMultiElementsSection

        NotationMultiElementView {
            model: root.sectionModel
            navigationPanel: navPanel
            navigationRowStart: root.navigation.row + 1
            anchorItem: root.anchorItem

            notationView: root.notationView

            onEnsureContentVisibleRequested: function(invisibleContentHeight) {
                root.ensureContentVisibleRequested(-invisibleContentHeight)
            }

            onPopupOpened: {
                root.popupOpened(openedPopup)
            }
        }
    }

    Component {
        id: notationSingleElementSection

        NotationSingleElementView {
            model: root.sectionModel
            navigationPanel: navPanel
            navigationRowStart: root.navigation.row + 1

            notationView: root.notationView

            onPopupOpened: {
                root.popupOpened(openedPopup)
            }
        }
    }

    Component {
        id: scoreSection

        ScoreDisplayInspectorView {
            model: root.sectionModel
            navigationPanel: navPanel
            navigationRowStart: root.navigation.row + 1

            notationView: root.notationView

            onPopupOpened: {
                root.popupOpened(openedPopup)
            }
        }
    }

    Component {
        id: scoreAppearanceSection

        ScoreAppearanceInspectorView {
            model: root.sectionModel
            navigationPanel: navPanel
            navigationRowStart: root.navigation.row + 1
            anchorItem: root.anchorItem

            notationView: root.notationView

            onEnsureContentVisibleRequested: function(invisibleContentHeight) {
                root.ensureContentVisibleRequested(-invisibleContentHeight)
            }

            onPopupOpened: {
                root.popupOpened(openedPopup)
            }
        }
    }
}
