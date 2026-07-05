#ifndef HUANGSHAN_COMPAT_SDK25_H
#define HUANGSHAN_COMPAT_SDK25_H

#include "lvsf_font.h"
#include "lvsf_input.h"
#include "lvsf_gesture.h"
#include "gui_app_fwk.h"

#ifdef BUILTIN_APP_EXPORT
#undef BUILTIN_APP_EXPORT
#endif
#define BUILTIN_APP_EXPORT(name, icon, id, entry) \
    SECTION_ITEM_REGISTER(BuiltinApp1Tab, RT_USED static const builtin_app_desc_t CONCAT_2(__builtinapp1_, __LINE__)) = \
    { name, icon, id, (gui_app_entry_func_ptr_t)entry, GUI_APP_BUILTIN }; \
    SECTION_ITEM_REGISTER(BuiltinApp2Tab, RT_USED static const builtin_app_desc_t CONCAT_2(__builtinapp2_, __LINE__)) = \
    { name, icon, id, (gui_app_entry_func_ptr_t)entry, GUI_APP_BUILTIN }

#define gui_app_init() gui_app_init(0)
#define gui_script_app_list_get_next(desc) gui_script_app_list_get_next((desc), SCRIPT_TYPE_QJS)
#define gui_script_watch_face_register() gui_script_watch_face_register(SCRIPT_TYPE_QJS)

#endif /* HUANGSHAN_COMPAT_SDK25_H */
