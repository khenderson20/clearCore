#include "nsc_qt/dock_panels.h"

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>

namespace nsc::qt {

ads::CDockWidget* addDockPanel(ads::CDockManager* manager, ads::CDockAreaWidget*& area,
                               const QString& title, QWidget* contents) {
    auto* dock = new ads::CDockWidget(manager, title);
    dock->setObjectName(title);
    dock->setWidget(contents);  // the content must live inside the dock; without
                                // this the panel renders empty (orphaned widget)
    if (area == nullptr)
        area = manager->addDockWidget(ads::CenterDockWidgetArea, dock);
    else
        manager->addDockWidgetTabToArea(dock, area);
    return dock;
}

}  // namespace nsc::qt
