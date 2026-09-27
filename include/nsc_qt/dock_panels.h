#pragma once

#include <QString>

namespace ads {
class CDockManager;
class CDockAreaWidget;
class CDockWidget;
}  // namespace ads

class QWidget;

namespace nsc::qt {

// Creates a dock named `title` that holds `contents`, tabbed into `area` (or, if
// `area` is null, starting a new central dock area and assigning it to `area`).
// Returns the created dock.
//
// This centralizes the "a panel is a dock that carries its content widget"
// invariant in one tested place. A regression here (e.g. dropping setWidget)
// once shipped an all-panels-empty GUI, because the content widgets were left
// orphaned in the QMainWindow instead of placed inside their docks — see
// qt_ui_test's dock-content check.
ads::CDockWidget* addDockPanel(ads::CDockManager* manager, ads::CDockAreaWidget*& area,
                               const QString& title, QWidget* contents);

}  // namespace nsc::qt
