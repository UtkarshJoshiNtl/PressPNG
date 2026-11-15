#include "gui_types.h"

namespace presspng::gui {

void registerMetaTypes() {
    qRegisterMetaType<Job>("presspng::gui::Job");
    qRegisterMetaType<JobResult>("presspng::gui::JobResult");
}

} // namespace presspng::gui
