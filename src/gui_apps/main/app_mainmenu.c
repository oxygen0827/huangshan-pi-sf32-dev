/*********************
 *      INCLUDES
 *********************/
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <dfs_posix.h>
#include "littlevgl2rtt.h"
#include "lvgl.h"
#include "lvsf.h"
#include "gui_app_fwk.h"
#ifdef DL_APP_SUPPORT
    #include "gui_dl_app_utils.h"
    #include "dlfcn.h"
#endif
#define DBG_LEVEL  DBG_LOG

//#include "EventRecorder.h"
#include "lv_ext_resource_manager.h"
#include "cell_transform.h"
#include "log.h"
#include "custom_trans_anim.h"


LV_IMG_DECLARE(human1);
LV_IMG_DECLARE(human2);
LV_IMG_DECLARE(eco);
LV_IMG_DECLARE(weather);
LV_IMG_DECLARE(house);
LV_IMG_DECLARE(clock_80);


LV_IMG_DECLARE(img_activity);
LV_IMG_DECLARE(img_alarm);
LV_IMG_DECLARE(img_alarm_2);
LV_IMG_DECLARE(img_calendar);
LV_IMG_DECLARE(img_camera);
LV_IMG_DECLARE(img_clock);
LV_IMG_DECLARE(img_group);
LV_IMG_DECLARE(img_itunes);
LV_IMG_DECLARE(img_mail);
LV_IMG_DECLARE(img_maps);
LV_IMG_DECLARE(img_messages);
LV_IMG_DECLARE(img_passbook);
LV_IMG_DECLARE(img_phone);
LV_IMG_DECLARE(img_photos);
LV_IMG_DECLARE(img_remote);
LV_IMG_DECLARE(img_settings);
LV_IMG_DECLARE(img_stocks);
LV_IMG_DECLARE(img_stopwatch);
LV_IMG_DECLARE(img_workout);
LV_IMG_DECLARE(img_world_clock);
//LV_IMG_DECLARE(celluar);


#define APP_ID "Main"

#define HUANGSHAN_HOME_SAFE_LEFT 28
#define HUANGSHAN_HOME_SAFE_RIGHT 28
#define HUANGSHAN_HOME_SAFE_TOP 34
#define HUANGSHAN_HOME_SAFE_BOTTOM 34
#define HUANGSHAN_HOME_DETAIL_EDGE_WIDTH 48
#define HUANGSHAN_HOME_DETAIL_BACK_DX 50
#define HUANGSHAN_HOME_DETAIL_BACK_MAX_DY 120
#define HUANGSHAN_HOME_REFRESH_MS 1500
#define HUANGSHAN_RUNTIME_APP_ROOT "/sdcard/apps"
#define HUANGSHAN_RUNTIME_ACTIVE_APP_FILE HUANGSHAN_RUNTIME_APP_ROOT "/.active"


//#define DEBUG_APP_MAINMENU_DISPLAY_ICON_COORDINATE

/*a virtual circle include gap between icons*/
#if (LV_VER_RES_MAX > LV_HOR_RES_MAX)
    #define ICON_OUTER_RADIUS  (LV_VER_RES_MAX / 9)
#else
    #define ICON_OUTER_RADIUS  (LV_HOR_RES_MAX / 9)
#endif

#define ICON_OUTER_DIAMETER  (ICON_OUTER_RADIUS * 2)
/*full fill with picture*/
#define ICON_INNER_RADIUS  ((ICON_OUTER_RADIUS * 8)/9)
#define ICON_INNER_DIAMETER  (ICON_INNER_RADIUS * 2)


#define MAX_APP_ROW_NUM   16
#define MAX_APP_COL_NUM   16

#define ICON_IMG_WIDTH      ICON_OUTER_DIAMETER //ICON_INNER_DIAMETER
#define ICON_IMG_HEIGHT     ICON_OUTER_DIAMETER //ICON_INNER_DIAMETER

#if (MAX_APP_ROW_NUM > MAX_APP_COL_NUM)
    #define PAGE_SCRL_WIDTH   ((ICON_OUTER_DIAMETER * (MAX_APP_ROW_NUM - 1)) + LV_HOR_RES_MAX)
#else
    #define PAGE_SCRL_WIDTH   ((ICON_OUTER_DIAMETER * (MAX_APP_COL_NUM - 1)) + LV_HOR_RES_MAX)
#endif

#define PAGE_SCRL_HEIGHT  (PAGE_SCRL_WIDTH * 10 / 7)

/* Columun0 Row0 icon pivot coordinate*/
#define C0R0_COORD_X (PAGE_SCRL_WIDTH >> 1)
#define C0R0_COORD_Y (0)


//#define DEBUG_APP_MAINMENU_DISPLAY_ICON_PARAM

#ifndef DEBUG_APP_MAINMENU_DISPLAY_ICON_PARAM
    #define   LIMIT_RECT_WIDTH   (LV_HOR_RES_MAX - 16)
    #define   LIMIT_RECT_HEIGHT  (LV_VER_RES_MAX - 20)
    #if (LV_VER_RES_MAX > LV_HOR_RES_MAX)
        #define   LIMIT_ROUND_RADIUS (LV_VER_RES_MAX >> 1)
    #else
        #define   LIMIT_ROUND_RADIUS (LV_HOR_RES_MAX >> 1)
    #endif
#else

    uint16_t LIMIT_RECT_WIDTH   = (LV_HOR_RES_MAX - 16);
    uint16_t LIMIT_RECT_HEIGHT  = (LV_VER_RES_MAX - 20);
    uint16_t LIMIT_ROUND_RADIUS = (LV_VER_RES_MAX >> 1);

    uint16_t LIMIT_ENABLE = 1;

#endif /* DEBUG_APP_MAINMENU_DISPLAY_ICON_PARAM */


#if defined(LCD_USING_ROUND_TYPE1) || defined(LCD_USING_ROUND_TYPE2_EVB_Z0) || defined(LCD_USING_ROUND_TYPE1_EVB_Z0)
    #define APP_MAINMENU_ROUND_SCREEN
#endif


/**
 * iterate over builtin app list
 */
extern const builtin_app_desc_t *gui_builtin_app_list_open(void);
extern const builtin_app_desc_t *gui_builtin_app_list_get_next(const builtin_app_desc_t *ptr_app);
extern void gui_builtin_app_list_close(const builtin_app_desc_t *ptr_app);


/**
 ***********************  Dynamic load app description ************************************************
 */
/**
 * system registered dl-app file description
 */
typedef struct
{
    char id[GUI_APP_ID_MAX_LEN];                    //!< dl_app id,an unique character id of an app (both built-in app and dl app)
    char dir[GUI_DL_APP_MAX_FILE_PATH_LEN];         //!< dl_app store root directory
    char name[GUI_APP_NAME_MAX_LEN];                //!< dl_app display name
    char icon[GUI_DL_APP_MAX_FILE_PATH_LEN];        //!< dl_app display icon relative path, base on it's root directory
    char exe_file[GUI_DL_APP_MAX_FILE_PATH_LEN];    //!< dl_app executable file relative path, base on root directory
} dl_app_reg_desc_t;

/**
 * iterate over registed dl_app_list
 */
extern const char *gui_dl_app_list_open(void);
extern rt_err_t gui_dl_app_list_get_next(const char **ppf_buff, dl_app_reg_desc_t *record);
extern void gui_dl_app_list_close(const char *ptr_app);

#define GUI_APP_CMD_MAX_LEN 32

typedef struct
{
    char name[GUI_APP_NAME_MAX_LEN];
    lv_img_dsc_t icon;
    char cmd[GUI_APP_CMD_MAX_LEN];

    uint8_t row;   //!< TODO: remove me
    uint8_t col;  //!< TODO: remove me


    rt_list_t node;
} app_mainmenu_item_t;

typedef struct
{
    uint8_t num;
    lv_obj_t *scr;
    lv_obj_t *pg_obj;

    uint8_t row_num;

    lv_obj_t **list;
#ifdef DEBUG_APP_MAINMENU_DISPLAY_ICON_COORDINATE
    lv_obj_t **label_list;
    lv_obj_t *param_ctrl[4];
#endif

    //Pivot before transformed
    lv_point_t *icon_pivot;

    /* current screen centern icon*/
    lv_obj_t *cicon;

    /*Focus/Zoom anim obj*/
    lv_obj_t *anim_obj;

    /*
        RANGE(0 ~ 2)

        = 1  no zoom
        < 1  zoom out
        > 1  zoom in

        NOTE:
        Icons only transform while zoom belong (0, 1],
        and zoom will be applied as tranformation ratio.
    */
    float zoom;
    float last_zoom;
    lv_point_t comp_vect;

    lv_indev_t *indev;
    bool springback_open;

    /*
        We NOT use lv_obj scroll, so here are scroll states
    */
    bool scroll_actived;
    lv_point_t scroll_sum;
} app_mainmenu_ctx_t;

#ifndef BSP_USING_LVGL_INPUT_AGENT
    static
#endif
app_mainmenu_ctx_t app_mainmenu_ctx;
//static rt_list_t app_list;

static void icon_event_callback(lv_event_t *e);
static void page_event_callback(lv_event_t *e);
static void get_icon_col_row(uint16_t idx, uint16_t *p_col, uint16_t *p_row);
static int layout_icon_transform(uint32_t row_idx, uint32_t col_idx, float *p_float_x, float *p_float_y, float *p_float_icon_w, float *p_float_icon_h, float *p_float_pivot_r);
static void app_mainmenu_icons_transform(bool force_refresh);


static bool limit_square(lv_area_t *parent_area, float *x, float *y, float *icon_r)
{
    float res_x1, res_x2, res_y1, res_y2;


    /* Get the smaller area from 'a1_p' and 'a2_p' */
    res_x1 = LV_MAX((float) parent_area->x1, *x - *icon_r);
    res_y1 = LV_MAX((float) parent_area->y1, *y - *icon_r);
    res_x2 = LV_MIN((float) parent_area->x2, *x + *icon_r);
    res_y2 = LV_MIN((float) parent_area->y2, *y + *icon_r);

    /*If x1 or y1 greater then x2 or y2 then the areas union is empty*/
    bool union_ok = true;
    if ((res_x1 > res_x2) || (res_y1 > res_y2))
    {
        *icon_r = 0;
        union_ok = false;
    }
    else
    {
        float new_w, new_h;

        new_w = res_x2 - res_x1 + 1;
        new_h = res_y2 - res_y1 + 1;

        *icon_r = LV_MIN((new_w / 2), (new_h / 2));
        *x = res_x1 + (new_w / 2);
        *y = res_y1 + (new_h / 2);
    }

    return union_ok;
}

static int cal_dist(uint16_t x, uint16_t y, lv_point_t *pivot)
{
    int r;

    r = (x - pivot->x) * (x - pivot->x) + (y - pivot->y) * (y - pivot->y);
    {
        lv_sqrt_res_t ds;
        lv_sqrt(r, &ds, 0x8000);
        r = ds.i;
    }
    return r;
}

static void limit_round(uint16_t limit_r, lv_point_t *zoom_pivot, lv_coord_t *x, lv_coord_t *y, uint16_t *icon_r, uint16_t pivot_r)
{
    if (pivot_r == 0)
        pivot_r = cal_dist(*x, *y, zoom_pivot);
    if (pivot_r + *icon_r > limit_r)
    {
        if (pivot_r - *icon_r < limit_r)
        {
            int32_t new_pivot_r;
            int32_t old_w, old_h;

            old_w = *x - zoom_pivot->x;
            old_h = *y - zoom_pivot->y;

            *icon_r = (limit_r - (pivot_r - *icon_r)) >> 1;
            new_pivot_r = limit_r - *icon_r;

            *x += (old_w * new_pivot_r / pivot_r) - old_w;
            *y += (old_h * new_pivot_r / pivot_r) - old_h;

        }
        else
            *icon_r = 0;
    }
}

static void limit_round2(float limit_r, lv_point_t *zoom_pivot, float *x, float *y, float *icon_r, float pivot_r)
{
    if (pivot_r + *icon_r > limit_r)
    {
        if (pivot_r - *icon_r < limit_r)
        {
            float new_pivot_r;
            float old_w, old_h;

            old_w = *x - (float) zoom_pivot->x;
            old_h = *y - (float) zoom_pivot->y;

            *icon_r = (limit_r - (pivot_r - *icon_r)) / 2;
            new_pivot_r = limit_r - *icon_r;

            *x += (old_w * new_pivot_r / pivot_r) - old_w;
            *y += (old_h * new_pivot_r / pivot_r) - old_h;

        }
        else
            *icon_r = 0;
    }
}

static lv_obj_t **get_icon_obj(uint32_t row_idx, uint32_t col_idx)
{
    if ((row_idx  >= MAX_APP_ROW_NUM) || (col_idx >= MAX_APP_COL_NUM))
    {
        return NULL;
    }

    return &(app_mainmenu_ctx.list[col_idx * MAX_APP_ROW_NUM + row_idx]);
}


static lv_point_t *get_icon_pivot(uint32_t row_idx, uint32_t col_idx)
{
    if ((row_idx  >= MAX_APP_ROW_NUM) || (col_idx >= MAX_APP_COL_NUM))
    {
        return NULL;
    }

    return &(app_mainmenu_ctx.icon_pivot[col_idx * MAX_APP_ROW_NUM + row_idx]);
}


/**
 * get icon colume and row by index as below(idx 0 row=(MAX_APP_ROW_NUM >> 1), col=(MAX_APP_COL_NUM >> 1)):
 *
 *     19___20___21___22
 *      |              \
 *      |  7___8___9   23
 *      |  |        \    \
 *     18  | 1___2   10   24
 *    /    |  \   \   \    \
 *   17    6   0   3   11   25
 *    \     \     /   /     /
 *     16    5___4   12    26
 *      \           /      /
 *       15___14___13    27
 * \n
 *
 * @param i
 * @param p_col
 * @param p_row
 * \n
 * @see
 */
static void layout_get_icon_col_row_by_idx(uint16_t idx, uint16_t *p_col, uint16_t *p_row)
{
    int16_t col, row;
    uint16_t i, total, hexagon_r;

    uint16_t one_edge_icons, hexagon_icons;

    if (0 != idx)
    {
        //find which hexagon is this icon on
        total = 0, hexagon_r = 0, hexagon_icons = 1;
        while (total + hexagon_icons - 1 < idx)
        {
            total += hexagon_icons;
            hexagon_r++;
            hexagon_icons = hexagon_r * 6;
        }

        //icons on one edge of this hexagon
        one_edge_icons = hexagon_r + 1;
        //first icon's  row&col num of this hexagon
        row = 0;
        col = 0 - hexagon_r;

        //calculate row&col from first one to idx
        for (i = 0; i < hexagon_icons; i++)
        {
            if (total + i == idx)
                break;

            switch (i / (one_edge_icons - 1))
            {
            case 0:
                col++;
                row--;
                break;

            case 1:
                col++;
                break;

            case 2:
                row++;
                break;

            case 3:
                col--;
                row++;
                break;

            case 4:
                col--;
                break;

            case 5:
                row--;
                break;

            default:
                RT_ASSERT(0);
                break;
            }

        }
    }
    else
    {
        col = 0;
        row = 0;
    }

    //rt_kprintf("icon %d, \t [%d,%d]\n",idx, row,col);
    *p_col = col + (MAX_APP_COL_NUM >> 1);
    *p_row = row + (MAX_APP_ROW_NUM >> 1);
}

static rt_err_t get_icon_col_row_by_lv_obj(lv_obj_t *obj, uint8_t *row_idx, uint8_t *col_idx)
{
    uint8_t i, j;

    if (NULL == obj)
    {
        return RT_EEMPTY;
    }

    for (i = 0; i < MAX_APP_ROW_NUM; i++)
        for (j = 0; j < MAX_APP_COL_NUM; j++)
        {
            if (obj == *get_icon_obj(i, j))
            {
                *row_idx = i;
                *col_idx = j;
                return RT_EOK;
            }
        }

    return RT_EEMPTY;
}

static void printf_icon_col_row(lv_obj_t *obj)
{
    uint8_t row_idx, col_idx;

    if (RT_EOK == get_icon_col_row_by_lv_obj(obj, &row_idx, &col_idx))
    {
        //LOG_I("printf_icon_col_row %x [%d,%d]", obj, row_idx, col_idx);
    }
}


static lv_obj_t *get_nearest_icon(lv_point_t *target)
{
    lv_obj_t *ret_v = NULL;
    uint8_t row_idx, col_idx;
    uint8_t first = 1;
    float min_delta = 0, pivot_r;

//    rt_kprintf("get_nearest_icon_vect  target %d, %d\n", target->x, target->y);
    for (row_idx = 0; row_idx < MAX_APP_ROW_NUM; row_idx++)
        for (col_idx = 0; col_idx < MAX_APP_COL_NUM; col_idx++)
        {
            uint32_t temp;
            lv_coord_t x, y;

            lv_sqrt_res_t ds;

            x = get_icon_pivot(row_idx, col_idx)->x;
            y = get_icon_pivot(row_idx, col_idx)->y;

            temp = (x - target->x) * (x - target->x) + (y - target->y) * (y - target->y);

            lv_sqrt(temp, &ds, 0x8000);

            pivot_r = ds.i + ds.f / 256;

            if (((pivot_r < min_delta) || first) && (NULL != *get_icon_obj(row_idx, col_idx)))
            {
                //vect2target->x =  target->x - x;
                //vect2target->y =  target->y - y;

                ret_v = *get_icon_obj(row_idx, col_idx);

                //rt_kprintf("get_nearest_icon_vect  [%d,%d] %d,%d vect %d, %d, %.1f\n", row_idx, col_idx, x, y, vect2target->x, vect2target->y, pivot_r);
                min_delta = pivot_r;

                first = 0;
            }
        }

    LV_ASSERT_NULL(ret_v);
    return  ret_v;
}


static lv_obj_t *add_app_icon(lv_obj_t *parent, const char *cmd, const void *img, uint8_t row_idx, uint8_t col_idx)
{
    lv_obj_t *icon;
    uint16_t s_len;
    char *cmd_str;
    if ((row_idx  >= MAX_APP_ROW_NUM) || (col_idx >= MAX_APP_COL_NUM))
    {
        return NULL;
    }

    icon = lv_img_create(parent);

    s_len = strlen(cmd) + 1;
    cmd_str = lv_mem_alloc(s_len);
    memcpy(cmd_str, cmd, s_len - 1);
    cmd_str[s_len - 1] = 0;

    lv_obj_set_user_data(icon, (void *)cmd_str);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(icon, icon_event_callback, LV_EVENT_ALL, 0);
    lv_img_set_src(icon, img); //lv_img_set_src(icon, LV_EXT_IMG_GET(img_mail)); //
    //lv_obj_set_style_img_opa(icon, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);

    //lv_page_glue_obj(icon, true);
    //lv_obj_set_parent_event(icon, true);
    *get_icon_obj(row_idx, col_idx) = icon;

    LV_ASSERT(lv_obj_get_self_width(icon) != 0);
    LV_ASSERT(lv_obj_get_self_height(icon) != 0);

#ifdef DEBUG_APP_MAINMENU_DISPLAY_ICON_COORDINATE
    {
        lv_obj_t *label;

        label = lv_label_create(parent, NULL);
        lv_ext_set_local_font(label, FONT_SMALL, LV_COLOR_WHITE);

        *get_label_obj(row_idx, col_idx) = label;
    }
#endif

    return icon;
}

static void get_icons_init_coordinate(uint32_t row_idx, uint32_t col_idx,    lv_coord_t *x, lv_coord_t *y)
{
    /*
       r3r2r1r0    c0c1c2c3
        \ \ \ \    / / / /
         \ \ \ \  / / / /
          \ \ \ \/ / / /
           \ \ \/\/ / /
            \ \/\/\/ /
             \/\/\/\/
             /\/\/\/\
            / /\/\/\ \
           / / /\/\ \ \
          / / / /\ \ \ \
         / / / /  \ \ \ \
        / / / /    \ \ \ \
       /
      /
     /  60 degree
    /________________________

    * (r0,c0) as (0,0) default

    */


    *x = ((col_idx - row_idx) * lv_trigo_cos(60) * ICON_OUTER_RADIUS) >> (LV_TRIGO_SHIFT - 1);
    *y = ((col_idx + row_idx) * lv_trigo_sin(60) * ICON_OUTER_RADIUS) >> (LV_TRIGO_SHIFT - 1);

    *x += C0R0_COORD_X;
    *y += C0R0_COORD_Y;


}




static int layout_icon_transform(uint32_t row_idx, uint32_t col_idx, float *p_float_x, float *p_float_y, float *p_float_icon_w, float *p_float_icon_h, float *p_float_pivot_r)
{
    float float_x = *p_float_x;
    float float_y = *p_float_y;
    float float_icon_r = (*p_float_icon_w) / 2;
    float pivot_r;      //Icon pivot to screen pivot

    lv_point_t scr_center;

    scr_center.x = LV_HOR_RES_MAX >> 1;
    scr_center.y = LV_VER_RES_MAX >> 1;

    lv_area_t parent_area;
    parent_area.x1 = (LV_HOR_RES_MAX - LIMIT_RECT_WIDTH) >> 1;
    parent_area.y1 = (LV_VER_RES_MAX - LIMIT_RECT_HEIGHT) >> 1;
    parent_area.x2 = parent_area.x1 + LIMIT_RECT_WIDTH - 1;
    parent_area.y2 = parent_area.y1 + LIMIT_RECT_HEIGHT - 1;


    //calculate draw pivot and radius
    if (get_icon_transform_param(float_x,
                                 float_y,
                                 float_icon_r,
                                 &float_x, &float_y, &float_icon_r, &pivot_r, LV_HOR_RES_MAX, LV_VER_RES_MAX))
    {


#ifdef DEBUG_APP_MAINMENU_DISPLAY_ICON_PARAM
        if (LIMIT_ENABLE)
#endif /* DEBUG_APP_MAINMENU_DISPLAY_ICON_PARAM */
        {
#ifndef APP_MAINMENU_ROUND_SCREEN
            limit_round2(LIMIT_ROUND_RADIUS, &scr_center, &float_x, &float_y, &float_icon_r, pivot_r);
            limit_square(&parent_area, &float_x, &float_y, &float_icon_r);


#else //round screen
            limit_round2(LV_HOR_RES >> 1, &scr_center, &float_x, &float_y, &float_icon_r, pivot_r);
            //limit_round(LV_HOR_RES >> 1, &scr_center, &x, &y, &icon_r, pivot_r);
#endif
        }


    }
    else
    {
        float_icon_r = 0;
        float_x = 0;
        float_y = 0;

    }

#if 1 //Gap

    if (float_icon_r >= (ICON_OUTER_RADIUS - ICON_INNER_RADIUS))
    {
        float_icon_r = float_icon_r - (ICON_OUTER_RADIUS - ICON_INNER_RADIUS);
    }
    else
    {
        float_icon_r = 0;
    }
#endif /* 0 */



    *p_float_x = float_x ;
    *p_float_y = float_y ;
    *p_float_icon_w = float_icon_r * 2;
    *p_float_pivot_r = pivot_r;      //Icon pivot to screen pivot


    return (0 == float_icon_r) ? 0 : 1;
}





#ifdef DEBUG_APP_MAINMENU_DISPLAY_ICON_COORDINATE
static void app_mainmenu_draw_icon_label(uint8_t row_idx, uint8_t col_idx, lv_coord_t pi_x, lv_coord_t pi_y, uint16_t r)
{
    lv_obj_t *label, *icon;
    char buff[50];

    label = app_mainmenu_ctx.label_list[row_idx][col_idx];
    icon = app_mainmenu_ctx.list[row_idx][col_idx];

    rt_sprintf(buff, "%d,%d,%d\n%d,%d,%d", lv_obj_get_x(icon), lv_obj_get_y(icon), lv_img_get_zoom(icon),
               pi_x, pi_y, r);
    lv_label_set_text(label, buff);
    lv_obj_align(label, icon, LV_ALIGN_CENTER, 0, 0);
}
#endif

static int32_t app_mainmenu_draw_icon(lv_obj_t *obj, float pi_x, float pi_y, float w, float h)
{
    uint16_t zoom;

    if ((w != 0) && (h != 0))
    {
        lv_coord_t img_w = lv_obj_get_self_width(obj);
        lv_coord_t img_h = lv_obj_get_self_height(obj);

        //lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        obj->flags &= (~LV_OBJ_FLAG_HIDDEN);


        //Updata zoom
        zoom = (uint16_t)(w * 256 / (float)img_w);
        //lv_img_set_zoom(obj, zoom);
#ifndef DISABLE_LVGL_V8
        ((lv_img_t *)obj)->zoom = zoom;
#else
        ((lv_img_t *)obj)->scale_x = zoom;
        ((lv_img_t *)obj)->scale_y = zoom;
#endif

        //Move icon
        {
            int32_t pi_x_10p8 = pi_x * 256;
            int32_t pi_y_10p8 = pi_y * 256;

            //rt_kprintf("app_mainmenu_draw_icon %p:  [%.3f,%.3f]->[%d, %d]   %d\n", obj, pi_x, pi_y, pi_x_10p8, pi_y_10p8, zoom);

            pi_x_10p8 -= ((img_w >> 1) << 8);
            pi_y_10p8 -= ((img_h >> 1) << 8);
            //lv_obj_set_pos(obj, ((lv_coord_t)(pi_x_10p8 >> 8)) + lv_obj_get_scroll_x(app_mainmenu_ctx.pg_obj),
            //               ((lv_coord_t)(pi_y_10p8 >> 8)) + lv_obj_get_scroll_y(app_mainmenu_ctx.pg_obj));


            lv_obj_move_to(obj, ((lv_coord_t)(pi_x_10p8 >> 8)) + lv_obj_get_scroll_x(app_mainmenu_ctx.pg_obj),
                           ((lv_coord_t)(pi_y_10p8 >> 8)) + lv_obj_get_scroll_y(app_mainmenu_ctx.pg_obj));

#ifndef DISABLE_LVGL_V8
            lv_img_set_x_frac(obj, (uint16_t)((pi_x_10p8 << 8)) & 0xFFFF);
            lv_img_set_y_frac(obj, (uint16_t)((pi_y_10p8 << 8)) & 0xFFFF);
#endif
        }
        //srt_kprintf("app_mainmenu_draw_icon %p:  %d,%d   %d\n",obj, pi_x, pi_y, zoom);
    }
    else
    {
        //lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        obj->flags |= LV_OBJ_FLAG_HIDDEN;
        zoom = 0;
    }
    return zoom;
}

static void app_mainmenu_init_icons_coordinate(void)
{
    lv_coord_t x, y;
    uint8_t row_idx, col_idx;

    for (row_idx = 0; row_idx < MAX_APP_ROW_NUM; row_idx++)
        for (col_idx = 0; col_idx < MAX_APP_COL_NUM; col_idx++)
        {
            lv_obj_t *icon;
            get_icons_init_coordinate(row_idx, col_idx, &x, &y);

            get_icon_pivot(row_idx, col_idx)->x = x;
            get_icon_pivot(row_idx, col_idx)->y = y;

            icon = *get_icon_obj(row_idx, col_idx);
            if (icon)
            {
                lv_coord_t img_w = lv_obj_get_self_width(icon);
                lv_coord_t img_h = lv_obj_get_self_height(icon);
                uint16_t zoom = (uint16_t)(ICON_IMG_WIDTH * 256 / img_w);

                //rt_kprintf("init icon [%d,%d]  %d\n", x, y, zoom);

                lv_obj_set_pos(icon, x - (img_w >> 1), y - (img_h >> 1));
                lv_img_set_pivot(icon, (img_w >> 1), (img_h >> 1));
                lv_img_set_zoom(icon, zoom);

            }
        }

}



#if 0

static void app_mainmenu_click_icon_anim_ready_callback(lv_anim_t *a)
{
    if (app_mainmenu_ctx.anim_obj)
    {
        char *cmd = (char *)lv_obj_get_user_data(app_mainmenu_ctx.anim_obj);

        if (cmd)
        {
            rt_kprintf("app mainmenu icon click anim cbk\n");
            gui_app_run(cmd);
        }
        app_mainmenu_ctx.anim_obj = NULL;
    }
}


static void app_mainmenu_focus_icon(lv_obj_t *obj, uint32_t max_anim_ms, lv_anim_ready_cb_t cbk)
{
    //lv_coord_t c0r0_x, c0r0_y;
    lv_coord_t sx, sy, dx, dy;
    lv_obj_t *pg_scrl;
    uint8_t row_idx, col_idx;
    lv_area_t parent_area;


    LOG_I("app_mainmenu_focus_icon");
    if (RT_EOK != get_icon_col_row_by_lv_obj(obj, &row_idx, &col_idx))
        return;

    LOG_I("app_mainmenu_focus_icon: row %d, col %d", row_idx, col_idx);

    parent_area.x1 = 0;
    parent_area.y1 = 0;
    parent_area.x2 = parent_area.x1 + LV_HOR_RES - 1;
    parent_area.y2 = parent_area.y1 + LV_VER_RES - 1;

    pg_scrl = lv_page_get_scrl(app_mainmenu_ctx.pg_obj);
    sx = lv_obj_get_x(pg_scrl);
    sy = lv_obj_get_y(pg_scrl);


    dx = sx + ((parent_area.x2 + 1 - parent_area.x1) >> 1) - get_icon_pivot(row_idx, col_idx)->x;
    dy = sy + ((parent_area.y2 + 1 - parent_area.y1) >> 1) - get_icon_pivot(row_idx, col_idx)->y;

    /*
        rt_kprintf("pg_scrl=[%d,%d]\n icon=[%d,%d]\n ciotsc=[%d,%d]\n", lv_obj_get_x(pg_scrl),lv_obj_get_y(pg_scrl),
                                        get_icon_pivot(row_idx,col_idx)->x,
                                        get_icon_pivot(row_idx,col_idx)->y,
                                        app_mainmenu_ctx.ciotsc.x,
                                        app_mainmenu_ctx.ciotsc.y

                                        );
    */

    app_mainmenu_ctx.anim_obj = NULL;

    lv_anim_del(pg_scrl, NULL);

    if (max_anim_ms)
    {
        lv_anim_t ax;
        lv_anim_path_t path;
        uint16_t anim_duration;
        lv_sqrt_res_t ds;
        uint32_t abs_x, abs_y;

        if (app_mainmenu_ctx.indev)
        {
            app_mainmenu_ctx.indev->proc.types.pointer.drag_throw_vect.x = 0;
            app_mainmenu_ctx.indev->proc.types.pointer.drag_throw_vect.y = 0;
        }
        abs_x = (dx > sx) ? (dx - sx) : (sx - dx);
        abs_y = (dy > sy) ? (dy - sy) : (sy - dy);

        _lv_sqrt(abs_x * abs_x + abs_y * abs_y, &ds, 0x8000);

        /*
             Start animation except central icon
        */
        {
            anim_duration = max_anim_ms * ds.i / LV_HOR_RES_MAX ;
            anim_duration = LV_MIN(anim_duration, max_anim_ms);

            //rt_kprintf("app_mainmenu_focus_icon anim_duration %d ms\n", anim_duration);

            lv_anim_init(&ax);
            lv_anim_set_time(&ax, anim_duration);
            lv_anim_path_init(&path);
            lv_anim_set_path(&ax, &path);
            lv_anim_path_set_cb(&path, lv_anim_path_ease_in_out);

            lv_anim_set_var(&ax, pg_scrl);
            lv_anim_set_exec_cb(&ax, (lv_anim_exec_xcb_t) lv_obj_set_x);
            lv_anim_set_values(&ax, sx, dx);
            lv_anim_start(&ax);

            lv_anim_set_exec_cb(&ax, (lv_anim_exec_xcb_t) lv_obj_set_y);
            lv_anim_set_values(&ax, sy, dy);
            lv_anim_set_ready_cb(&ax, cbk);
            lv_anim_start(&ax);
            app_mainmenu_ctx.anim_obj = obj;

            return;
        }
    }
    lv_obj_set_pos(pg_scrl, dx, dy);

    if (cbk)
    {
        lv_anim_t ax;
        ax.var = obj;
        cbk(&ax);
    }


}
#endif

static void icon_event_callback(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_event_code_t event = lv_event_get_code(e);

    //rt_kprintf("icon_event_callback %s\n",lv_event_to_name(event));
    if (app_mainmenu_ctx.scroll_actived && (LV_EVENT_RELEASED == event \
                                            || LV_EVENT_PRESS_LOST == event))
    {
        //Not to clear scroll_actived before icon receieve click event
        lv_event_stop_bubbling(e);
    }
    else if ((LV_EVENT_SHORT_CLICKED == event) && (!app_mainmenu_ctx.scroll_actived))
    {
        rt_kprintf("app mainmenu icon clickd\n");

        lv_obj_add_flag(app_mainmenu_ctx.pg_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_scroll_to_view(obj, LV_ANIM_ON);
        app_mainmenu_ctx.anim_obj = obj;

        //There is no scroll animation if the clicked icon was just at center of pg_obj
        if (NULL == lv_anim_get(app_mainmenu_ctx.pg_obj, NULL))
        {
            //Send SCROLL_END msg manually
#ifndef  DISABLE_LVGL_V8
            lv_event_send(app_mainmenu_ctx.pg_obj, LV_EVENT_SCROLL_END, NULL);
#else
            lv_obj_send_event(app_mainmenu_ctx.pg_obj, LV_EVENT_SCROLL_END, NULL);
#endif
        }

    }
    else if (LV_EVENT_DELETE == event)
    {
        char *cmd = (char *)lv_obj_get_user_data(obj);
        if (cmd)
            lv_mem_free(cmd);
    }
}



static lv_obj_t *predict_focus_icon(void)
{
    lv_point_t scr_center;
    lv_point_t vect;
    uint8_t scroll_throw;
    uint32_t anim_duration;

    lv_indev_t *indev = lv_indev_get_act();

    scr_center.x = LV_HOR_RES_MAX >> 1;
    scr_center.y = LV_VER_RES_MAX >> 1;


    lv_indev_get_vect(indev, &vect);

    LOG_I("scroll_throw %d,%d", vect.x, vect.y);
#ifndef DISABLE_LVGL_V8
    scroll_throw = indev->driver->scroll_throw;
#else
    scroll_throw = 0;
#endif
    anim_duration = 0;

    while (vect.x != 0  || vect.y != 0)
    {
        /*Reduce the vectors*/
        vect.x = vect.x * (100 - scroll_throw) / 100;
        vect.y = vect.y * (100 - scroll_throw) / 100;

        scr_center.x = scr_center.x - vect.x;
        scr_center.y = scr_center.y - vect.y;

        anim_duration += LV_INDEV_DEF_READ_PERIOD * 4;
    }

    /*
       Get the compensation vector of nearest icon to new screen center
    */
    //update_icon_pivot();
    //get_nearest_icon_vect(&scr_center, &app_mainmenu_ctx.comp_vect);

    return    get_nearest_icon(&scr_center);
}


static void page_event_callback(lv_event_t *e)
{

    lv_obj_t *obj = lv_event_get_target(e);
    lv_event_code_t event = lv_event_get_code(e);


    //LOG_I("page_event_callback:event %s", lv_event_to_name(event));
    if (LV_EVENT_PRESSED == event)
    {
        //Clear anim obj in case stop scroll to been aborted
        app_mainmenu_ctx.anim_obj = NULL;
        lv_obj_clear_flag(app_mainmenu_ctx.pg_obj, LV_OBJ_FLAG_SCROLLABLE);

        if (0 != lv_anim_count_running())
        {
            lv_anim_del(app_mainmenu_ctx.pg_obj, NULL);
            //lv_anim_del(lv_page_get_scrl(app_mainmenu_ctx.pg_obj), NULL);
        }
        app_mainmenu_ctx.springback_open = false;
    }
    else if (LV_EVENT_RELEASED == event \
             || LV_EVENT_PRESS_LOST == event || LV_EVENT_CLICKED == event)
    {
        lv_obj_add_flag(app_mainmenu_ctx.pg_obj, LV_OBJ_FLAG_SCROLLABLE);

        if (app_mainmenu_ctx.scroll_actived)
        {
            app_mainmenu_ctx.springback_open = true;

            LOG_I("predict focus icon:");
            printf_icon_col_row(predict_focus_icon());

            lv_obj_scroll_to_view(predict_focus_icon(), LV_ANIM_ON);
        }

        app_mainmenu_ctx.scroll_actived = false;
        app_mainmenu_ctx.scroll_sum.x = 0;
        app_mainmenu_ctx.scroll_sum.y = 0;

    }
    else if (LV_EVENT_PRESSING == event)
    {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t p;

        lv_indev_get_vect(indev, &p);

        app_mainmenu_ctx.scroll_sum.x += p.x;
        app_mainmenu_ctx.scroll_sum.y += p.y;

#ifndef DISABLE_LVGL_V8
        if ((LV_ABS(app_mainmenu_ctx.scroll_sum.x) > indev->driver->scroll_limit)
                || (LV_ABS(app_mainmenu_ctx.scroll_sum.y) > indev->driver->scroll_limit)
                || app_mainmenu_ctx.scroll_actived)
        {
            app_mainmenu_ctx.scroll_actived = true;
            //_lv_obj_scroll_by_raw(app_mainmenu_ctx.pg_obj, p.x, p.y); scroll once before draw to speed up
            lv_obj_invalidate(app_mainmenu_ctx.pg_obj);
        }
#endif

#ifdef DISABLE_LVGL_V8



        if (app_mainmenu_ctx.scroll_actived)
        {
            _lv_obj_scroll_by_raw(app_mainmenu_ctx.pg_obj,
                                  app_mainmenu_ctx.scroll_sum.x,
                                  app_mainmenu_ctx.scroll_sum.y);

            app_mainmenu_ctx.scroll_sum.x = 0;
            app_mainmenu_ctx.scroll_sum.y = 0;
        }
        app_mainmenu_icons_transform(false);
        printf_icon_col_row(app_mainmenu_ctx.cicon);
#endif /* DISABLE_LVGL_V8 */

    }
#ifndef DISABLE_LVGL_V8
    else if (LV_EVENT_DRAW_MAIN_BEGIN == event)
    {


        if (app_mainmenu_ctx.scroll_actived)
        {
            _lv_obj_scroll_by_raw(app_mainmenu_ctx.pg_obj,
                                  app_mainmenu_ctx.scroll_sum.x,
                                  app_mainmenu_ctx.scroll_sum.y);

            app_mainmenu_ctx.scroll_sum.x = 0;
            app_mainmenu_ctx.scroll_sum.y = 0;
        }
        app_mainmenu_icons_transform(false);
        printf_icon_col_row(app_mainmenu_ctx.cicon);
    }
#endif /* DISABLE_LVGL_V8 */
    else if (LV_EVENT_SCROLL_END == event)
    {
        if (app_mainmenu_ctx.anim_obj)
        {
            char *cmd = (char *)lv_obj_get_user_data(app_mainmenu_ctx.anim_obj);

            if (cmd)
            {
                rt_kprintf("app mainmenu icon click anim cbk\n");
                gui_app_run(cmd);
            }
            app_mainmenu_ctx.anim_obj = NULL;
        }
    }


}


static inline int16_t reorder_clock_icon(int16_t idx, int16_t clock_idx, const builtin_app_desc_t *builtin_app, lv_obj_t *page)
{
    uint16_t col, row;

    //Fix 1st icon for clock
    if (0 == strcmp("clock", builtin_app->id))
    {
        lv_obj_t *icon;
        layout_get_icon_col_row_by_idx(clock_idx, &col, &row);

        icon = add_app_icon(page, "clock", builtin_app->icon, row, col);

        if (icon)        lv_obj_move_to_index(icon, 0);
    }
    else if (0 == strcmp(APP_ID, builtin_app->id)) //skip main menu icon
    {

    }
    else if (NULL != builtin_app->icon)
    {
        if (idx == clock_idx)
            idx++;

        layout_get_icon_col_row_by_idx(idx, &col, &row);
        add_app_icon(page, builtin_app->id, builtin_app->icon, row, col);
        idx++;
    }

    return idx;
}



static void app_mainmenu_read_app_icons(lv_obj_t *page)
{
    uint16_t col, row;
    uint16_t idx = 0, clock_idx;
    const builtin_app_desc_t *p_builtin_app;
    int mainmenu_icon_style;

    clock_idx = 0; /* 0 - reserved for clock app*/
    mainmenu_icon_style = 0x00;


    /*1. load builtin app list*/
    p_builtin_app = gui_builtin_app_list_open();
    if (p_builtin_app)
    {
        do
        {
            //Fix 1st icon for clock
            idx = reorder_clock_icon(idx, clock_idx, p_builtin_app, page);
            p_builtin_app = gui_builtin_app_list_get_next(p_builtin_app);
        }
        while (p_builtin_app);

        while (1)
        {
            //Fix 1st icon for clock
            p_builtin_app = gui_script_app_list_get_next(p_builtin_app);
            if (p_builtin_app == NULL)
                break;
            idx = reorder_clock_icon(idx, clock_idx, p_builtin_app, page);
        }

        gui_builtin_app_list_close(p_builtin_app);
        p_builtin_app = NULL;
    }

#if 1//for demo, full fill screen
    {
        uint16_t i;
        const void *dummy_icons[] =
        {
            LV_EXT_IMG_GET(img_passbook),
            LV_EXT_IMG_GET(img_mail), LV_EXT_IMG_GET(img_calendar), LV_EXT_IMG_GET(img_camera),
            LV_EXT_IMG_GET(img_phone), LV_EXT_IMG_GET(img_alarm_2), LV_EXT_IMG_GET(img_maps),
            LV_EXT_IMG_GET(img_photos), LV_EXT_IMG_GET(img_remote), LV_EXT_IMG_GET(img_workout),
            LV_EXT_IMG_GET(img_world_clock), LV_EXT_IMG_GET(img_stocks),
            LV_EXT_IMG_GET(img_alarm), LV_EXT_IMG_GET(img_stocks),
            LV_EXT_IMG_GET(img_passbook),
            LV_EXT_IMG_GET(img_mail), LV_EXT_IMG_GET(img_calendar), LV_EXT_IMG_GET(img_camera),
            LV_EXT_IMG_GET(img_phone), LV_EXT_IMG_GET(img_alarm_2), LV_EXT_IMG_GET(img_maps),
            LV_EXT_IMG_GET(img_photos), LV_EXT_IMG_GET(img_remote), LV_EXT_IMG_GET(img_workout),
            LV_EXT_IMG_GET(img_world_clock), LV_EXT_IMG_GET(img_stocks),
            LV_EXT_IMG_GET(img_alarm), LV_EXT_IMG_GET(img_stocks),
            LV_EXT_IMG_GET(img_passbook),
            LV_EXT_IMG_GET(img_mail), LV_EXT_IMG_GET(img_calendar), LV_EXT_IMG_GET(img_camera),
            LV_EXT_IMG_GET(img_phone), LV_EXT_IMG_GET(img_alarm_2), LV_EXT_IMG_GET(img_maps),
            LV_EXT_IMG_GET(img_photos), LV_EXT_IMG_GET(img_remote), LV_EXT_IMG_GET(img_workout),
            LV_EXT_IMG_GET(img_world_clock), LV_EXT_IMG_GET(img_stocks),
            LV_EXT_IMG_GET(img_alarm), LV_EXT_IMG_GET(img_stocks),
            LV_EXT_IMG_GET(img_passbook),
            LV_EXT_IMG_GET(img_mail), LV_EXT_IMG_GET(img_calendar), LV_EXT_IMG_GET(img_camera),
            LV_EXT_IMG_GET(img_phone), LV_EXT_IMG_GET(img_alarm_2), LV_EXT_IMG_GET(img_maps),
            LV_EXT_IMG_GET(img_photos), LV_EXT_IMG_GET(img_remote), LV_EXT_IMG_GET(img_workout),
            LV_EXT_IMG_GET(img_world_clock), LV_EXT_IMG_GET(img_stocks),
            LV_EXT_IMG_GET(img_alarm), LV_EXT_IMG_GET(img_stocks),
        };

        for (i = 0; i < sizeof(dummy_icons) / sizeof(dummy_icons[0]); i++, idx++)
        {
            layout_get_icon_col_row_by_idx(idx, &col, &row);
            lv_obj_t *p_obj = add_app_icon(page, "none", dummy_icons[i], row, col);

            lv_obj_set_style_img_opa(p_obj, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
#endif

}
/**
 * move whole icons map together
 * \n
 *
 * @param c0r0_x - col 0, row 0 item coordinate x
 * @param c0r0_y - col 0, row 0 item coordinate y
 * \n
 * @see
 */
#define MM_ZOOM(A,B,zoom) ((A) + (((B) - (A)) * (zoom)))
static void app_mainmenu_icons_transform(bool force_refresh)
{
    lv_coord_t x_offset, y_offset;
    uint8_t row_idx, col_idx;
    float min_delta;
    static lv_coord_t c0r0_x, c0r0_y;

    lv_coord_t cur_c0r0_x = C0R0_COORD_X - lv_obj_get_scroll_x(app_mainmenu_ctx.pg_obj);
    lv_coord_t cur_c0r0_y = C0R0_COORD_Y - lv_obj_get_scroll_y(app_mainmenu_ctx.pg_obj);

    if (c0r0_x == cur_c0r0_x \
            && c0r0_y == cur_c0r0_y \
            && app_mainmenu_ctx.last_zoom == app_mainmenu_ctx.zoom \
            && !force_refresh)
    {
        return;
    }

    //rt_kprintf("app_mainmenu_icons_transform:zoom %f last_zoom %f force_refresh\n",app_mainmenu_ctx.zoom, app_mainmenu_ctx.last_zoom, force_refresh);

    c0r0_x = cur_c0r0_x;
    c0r0_y = cur_c0r0_y;

    x_offset = c0r0_x - get_icon_pivot(0, 0)->x;
    y_offset = c0r0_y - get_icon_pivot(0, 0)->y;

    min_delta = LV_VER_RES;

    //uint32_t start = rt_tick_get();

    for (row_idx = 0; row_idx < MAX_APP_ROW_NUM; row_idx++)
        for (col_idx = 0; col_idx < MAX_APP_COL_NUM; col_idx++)
        {
            //offset icon pivot
            get_icon_pivot(row_idx, col_idx)->x += x_offset;
            get_icon_pivot(row_idx, col_idx)->y += y_offset;

            if (*get_icon_obj(row_idx, col_idx))
            {
                float float_x = (float)get_icon_pivot(row_idx, col_idx)->x;
                float float_y = (float)get_icon_pivot(row_idx, col_idx)->y;
                float float_icon_w = (float) ICON_IMG_WIDTH;
                float float_icon_h = (float) ICON_IMG_HEIGHT;

                float pivot_r; //Icon pivot to screen pivot
                float zoom;

                //rt_kprintf("transf_before[%.1f,%.1f] w=%.1f,h=%.1f\n", float_x, float_y, float_icon_w, float_icon_h);

                if (app_mainmenu_ctx.zoom < 1)
                {
                    zoom = app_mainmenu_ctx.zoom;
                    zoom = zoom * zoom;
                    float_x = MM_ZOOM(LV_HOR_RES_MAX >> 1, float_x, zoom);
                    float_y = MM_ZOOM(LV_VER_RES_MAX >> 1, float_y, zoom);
                    float_icon_w *= zoom;
                    float_icon_h *= zoom;
                }
                if (app_mainmenu_ctx.zoom > 0)
                {
                    float float_x_before = float_x;
                    float float_y_before = float_y;
                    float float_icon_w_before = float_icon_w;
                    float float_icon_h_before = float_icon_h;

                    //calculate draw pivot and radius
                    if (layout_icon_transform(row_idx, col_idx, &float_x, &float_y, &float_icon_w, &float_icon_h, &pivot_r))
                    {
                        //Record the nearest icon to center
                        if (pivot_r < min_delta)
                        {
                            //rt_kprintf("center[%d,%d] pivot[%.1f, %.1f], float_pivot_r =%.2f  \n", row_idx, col_idx, float_x, float_y, pivot_r);

                            min_delta = pivot_r;
                            //app_mainmenu_ctx.ciotsc.x = float_x - scr_center.x;
                            //app_mainmenu_ctx.ciotsc.y = float_y - scr_center.y;
                            app_mainmenu_ctx.cicon = *get_icon_obj(row_idx, col_idx);
                        }

                        if ((app_mainmenu_ctx.zoom > 0) && (app_mainmenu_ctx.zoom <= 1))
                        {
                            zoom = app_mainmenu_ctx.zoom;
                            float_x = MM_ZOOM(float_x_before, float_x, zoom);
                            float_y = MM_ZOOM(float_y_before, float_y, zoom);

                            float_icon_w = MM_ZOOM(float_icon_w_before, float_icon_w, zoom);
                            float_icon_h = MM_ZOOM(float_icon_h_before, float_icon_w, zoom);
                        }
                    }
                    else
                    {
                        float_icon_w = 0;
                        float_icon_h = 0;
                        float_x = 0;
                        float_y = 0;
                    }
                }

                if (app_mainmenu_ctx.zoom > 1)
                {
                    zoom = 1 / (2 - app_mainmenu_ctx.zoom);

                    float_x = MM_ZOOM(LV_HOR_RES_MAX >> 1, float_x, zoom);
                    float_y = MM_ZOOM(LV_VER_RES_MAX >> 1, float_y, zoom);
                    float_icon_w *= zoom;
                    float_icon_h *= zoom;
                }

                //draw icon
                //rt_kprintf("transf_after[%.1f,%.1f] w=%.1f,h=%.1f\n", float_x, float_y, float_icon_w, float_icon_h);
                app_mainmenu_draw_icon(*get_icon_obj(row_idx, col_idx), float_x, float_y, float_icon_w, float_icon_h);

#ifdef DEBUG_APP_MAINMENU_DISPLAY_ICON_COORDINATE
                app_mainmenu_draw_icon_label(row_idx, col_idx, float_x, float_y, float_icon_w, float_icon_h, pivot_r);
#endif
            }
        }


    //rt_kprintf("app_mainmenu_icons_transform cost %d ms \n",rt_tick_get() - start);
}
/*
static void app_mainmenu_drag_springback(void)
{
    lv_point_t icons_pivot, dis;
    lv_obj_t *icons = app_mainmenu_ctx.cicon;
    icons_pivot.x = (lv_coord_t)((icons->coords.x2 - icons->coords.x1) >> 1) + icons->coords.x1;
    icons_pivot.y = (lv_coord_t)((icons->coords.y2 - icons->coords.y1) >> 1) + icons->coords.y1;

    dis.x = abs(icons_pivot.x - (LV_HOR_RES_MAX >> 1));
    dis.y = abs(icons_pivot.y - (LV_VER_RES_MAX >> 1));

    if (dis.x > lv_obj_get_width(app_mainmenu_ctx.cicon) \
            || dis.y > lv_obj_get_height(app_mainmenu_ctx.cicon))
    {
        app_mainmenu_ctx.springback_open = false;
        if (app_mainmenu_ctx.indev)
        {
            app_mainmenu_ctx.indev->proc.types.pointer.drag_throw_vect.x = 0;
            app_mainmenu_ctx.indev->proc.types.pointer.drag_throw_vect.y = 0;
        }
        if (0 != lv_anim_count_running())
        {
            lv_anim_del(app_mainmenu_ctx.pg_obj, NULL);
            lv_anim_del(lv_page_get_scrl(app_mainmenu_ctx.pg_obj), NULL);
        }
        //LOG_L1("app_mainmenu_drag_springback");
        app_mainmenu_focus_icon(app_mainmenu_ctx.cicon, 300, NULL);
    }
}


static lv_design_res_t mm_scrl_design(lv_obj_t *scrl, const lv_area_t *clip_area, lv_design_mode_t mode)
{
    lv_res_t res;

    if (mode == LV_DESIGN_DRAW_MAIN)
    {
        app_mainmenu_icons_transform(false);
        if (app_mainmenu_ctx.springback_open)
        {
            app_mainmenu_drag_springback();
        }
    }
    res = app_mainmenu_ctx.scrl_design(scrl, clip_area, mode);
    return res;
}

*/


void app_mainmenu_ui_init(void *param)
{
    lv_obj_t *page = lv_obj_create(lv_scr_act()); //lv_img_create(lv_scr_act()); //
    lv_obj_set_size(page, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(page, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_scroll_snap_x(page, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scroll_snap_y(page, LV_SCROLL_SNAP_CENTER);
    //lv_img_set_src(page,LV_EXT_IMG_GET(celluar));


    app_mainmenu_ctx.pg_obj = page;

    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(page, page_event_callback, LV_EVENT_ALL, 0);

    //app_mainmenu_ctx.scrl_design = lv_obj_get_design_cb(lv_page_get_scrl(page));

    //lv_obj_set_design_cb(lv_page_get_scrl(page), mm_scrl_design);

    app_mainmenu_read_app_icons(page);

    /* disable autofit so that scrollable part would not be auto repositioned if changing children size */
    //lv_page_set_scrllable_fit4(page, LV_FIT_NONE, LV_FIT_NONE, LV_FIT_NONE, LV_FIT_NONE);

#if 0//def OLD_LAYOUT
    /* add margin in the right */
    lv_page_set_scrl_width(page, lv_page_get_scrl_width(page) + lv_obj_get_width(app_mainmenu_ctx.list[0]));
#else
    /* add margin in the right */
    //lv_page_set_scrl_width(page, PAGE_SCRL_WIDTH);
    /* add margin at the bottom */
    //lv_page_set_scrl_height(page, PAGE_SCRL_HEIGHT);
#endif

    app_mainmenu_init_icons_coordinate();

    /*
        {
            uint16_t col, row;
            get_icon_col_row(0, &col, &row);
            app_mainmenu_focus_icon(*get_icon_obj(row, col), 0, NULL);
        }
        page_event_callback(app_mainmenu_ctx.pg_obj, LV_EVENT_RELEASED);

    */



}


static void destroy(void)
{

}

static lv_obj_t *get_main_win(void)
{
    return NULL;
}

static app_mainmenu_item_t *new_app_item(char *name, const lv_img_dsc_t *icon, char *cmd, uint8_t row, uint8_t col)
{
    app_mainmenu_item_t *item;
    rt_size_t len;

    RT_ASSERT((name != NULL) && (icon != NULL) && (cmd != NULL));

    item = (app_mainmenu_item_t *)rt_malloc(sizeof(app_mainmenu_item_t));

    if (item != NULL)
    {
        memset(item, 0, sizeof(app_mainmenu_item_t));

        len = strlen(name);
        RT_ASSERT(len <= (GUI_APP_NAME_MAX_LEN - 1));
        strncpy(item->name, name, len);
        item->name[len] = '\0';


        len = strlen(cmd);
        RT_ASSERT(len <= (GUI_APP_CMD_MAX_LEN - 1));
        strncpy(item->cmd, cmd, len);
        item->cmd[len] = '\0';

        memcpy(&item->icon, icon, sizeof(lv_img_dsc_t));

        item->row = row;
        item->col = col;
    }

    return item;
}
#ifdef APP_TRANS_ANIMATION_SCALE
    #define MM_CUST_TRAN_ANIMATION
#endif /* APP_TRANS_ANIMATION_SCALE */

#ifdef MM_CUST_TRAN_ANIMATION


static CUST_ANIM_TYPE_E cust_anim_type = CUST_ANIM_TYPE_0;
static void mm_trans_anim_init(void)
{
    //cust_anim_type = CUST_ANIM_TYPE_3; //Fix trans animation
    cust_trans_anim_config(cust_anim_type++, NULL);
//Avoid animation crash
#if (LV_HOR_RES_MAX > 512)||(LV_VER_RES_MAX > 512)
    if (cust_anim_type == CUST_ANIM_TYPE_3)  cust_anim_type++;
#endif

    if (cust_anim_type >= CUST_ANIM_TYPE_MAX) cust_anim_type = CUST_ANIM_TYPE_0;
}


#else
static void mm_trans_anim_init(void)
{
    gui_app_trans_anim_t enter_anim_cfg, exit_anim_cfg;

    gui_app_trans_anim_init_cfg(&enter_anim_cfg, GUI_APP_TRANS_ANIM_ZOOM_OUT);
    gui_app_trans_anim_init_cfg(&exit_anim_cfg, GUI_APP_TRANS_ANIM_ZOOM_IN);


    enter_anim_cfg.cfg.zoom.zoom_start = LV_IMG_ZOOM_NONE >> 2;
    enter_anim_cfg.cfg.zoom.zoom_end = LV_IMG_ZOOM_NONE;
    enter_anim_cfg.cfg.zoom.opa_start = LV_OPA_0;
    enter_anim_cfg.cfg.zoom.opa_end = LV_OPA_COVER;

    exit_anim_cfg.cfg.zoom.zoom_start = LV_IMG_ZOOM_NONE;
    exit_anim_cfg.cfg.zoom.zoom_end = LV_IMG_ZOOM_NONE << 2;
    exit_anim_cfg.cfg.zoom.opa_start = LV_OPA_COVER;
    exit_anim_cfg.cfg.zoom.opa_end = LV_OPA_0;


    gui_app_set_enter_trans_anim(&enter_anim_cfg);
    gui_app_set_exit_trans_anim(&exit_anim_cfg);

}
#endif /* MM_CUST_TRAN_ANIMATION */

typedef enum
{
    HUANGSHAN_HOME_CARD_RUNTIME_APP,
    HUANGSHAN_HOME_CARD_BUILTIN_APP,
} huangshan_home_card_kind_t;

#define HUANGSHAN_HOME_MAX_APPS 40
#define HUANGSHAN_HOME_TITLE_MAX 48
#define HUANGSHAN_HOME_SUBTITLE_MAX 56
#define HUANGSHAN_HOME_META_MAX 48
#define HUANGSHAN_HOME_REQUIREMENTS_MAX 96

#ifndef DT_DIR
#define DT_DIR 4
#endif

typedef struct
{
    char title[HUANGSHAN_HOME_TITLE_MAX];
    char subtitle[HUANGSHAN_HOME_SUBTITLE_MAX];
    char category[HUANGSHAN_HOME_META_MAX];
    char author[HUANGSHAN_HOME_META_MAX];
    char requirements[HUANGSHAN_HOME_REQUIREMENTS_MAX];
    char target[GUI_APP_CMD_MAX_LEN];
    huangshan_home_card_kind_t kind;
    uint32_t color;
    lv_coord_t x;
    lv_coord_t y;
    lv_coord_t w;
    lv_coord_t h;
} huangshan_home_card_t;

static lv_obj_t *huangshan_home_root;
static lv_obj_t *huangshan_home_status;
static lv_obj_t *huangshan_home_list;
static lv_obj_t *huangshan_home_detail;
static lv_timer_t *huangshan_home_refresh_timer;
static huangshan_home_card_t huangshan_home_cards[HUANGSHAN_HOME_MAX_APPS];
static int huangshan_home_card_count;
static const huangshan_home_card_t *huangshan_home_pending_delete;
static lv_point_t huangshan_home_detail_press;
static uint8_t huangshan_home_detail_tracking;
static uint8_t huangshan_home_refresh_on_resume;

static void huangshan_home_ui_init(void);

static void home_safe_copy(char *dst, rt_size_t cap, const char *src, const char *fallback)
{
    rt_size_t len;
    const char *value = (src && src[0]) ? src : (fallback ? fallback : "");
    if (!dst || cap == 0) return;
    len = strlen(value);
    if (len >= cap) len = cap - 1;
    memcpy(dst, value, len);
    dst[len] = '\0';
}

static void home_set_obj_bg(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t *home_create_label(lv_obj_t *parent, const char *text, uint16_t font_size,
                                   lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_ext_set_local_font(label, font_size, color);
    return label;
}

static void home_set_status(const char *text, lv_color_t color)
{
    if (!huangshan_home_status) return;
    lv_label_set_text(huangshan_home_status, text ? text : "");
    lv_obj_set_style_text_color(huangshan_home_status, color, 0);
}

static int home_write_active_runtime_app(const char *app_id)
{
    int fd;
    int len;
    int written;
    if (!app_id || !app_id[0]) return -RT_EINVAL;
    if (access(HUANGSHAN_RUNTIME_APP_ROOT, 0) != 0) return -RT_ERROR;
    fd = open(HUANGSHAN_RUNTIME_ACTIVE_APP_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -RT_ERROR;
    len = strlen(app_id);
    written = write(fd, app_id, len);
    if (written == len)
    {
        const char newline = '\n';
        (void)write(fd, &newline, 1);
    }
    close(fd);
    return written == len ? RT_EOK : -RT_ERROR;
}

static int home_is_safe_app_id(const char *id)
{
    const char *p = id;
    int len = 0;
    if (!id || !id[0]) return 0;
    if (!(*p >= 'a' && *p <= 'z')) return 0;
    for (; *p; p++)
    {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) return 0;
        len++;
        if (len >= GUI_APP_CMD_MAX_LEN) return 0;
    }
    return 1;
}

static int home_file_exists(const char *path)
{
    return path && access(path, 0) == 0;
}

static int home_path_is_dir(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) return 0;
    closedir(dir);
    return 1;
}

static int home_remove_tree(const char *path)
{
    DIR *dir;
    struct dirent *entry;
    int result = RT_EOK;
    if (!path || !path[0]) return -RT_EINVAL;
    dir = opendir(path);
    if (!dir)
    {
        return unlink(path) == 0 ? RT_EOK : -RT_ERROR;
    }
    while ((entry = readdir(dir)) != RT_NULL)
    {
        char child[180];
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        rt_snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        child[sizeof(child) - 1] = '\0';
        if (home_path_is_dir(child))
        {
            if (home_remove_tree(child) != RT_EOK) result = -RT_ERROR;
        }
        else if (unlink(child) != 0)
        {
            result = -RT_ERROR;
        }
    }
    closedir(dir);
    if (rmdir(path) != 0) result = -RT_ERROR;
    return result;
}

static int home_read_text_prefix(const char *path, char *dst, rt_size_t cap)
{
    int fd;
    int n;
    if (!path || !dst || cap == 0) return -RT_EINVAL;
    dst[0] = '\0';
    fd = open(path, O_RDONLY);
    if (fd < 0) return -RT_ERROR;
    n = read(fd, dst, cap - 1);
    close(fd);
    if (n < 0) return -RT_ERROR;
    dst[n] = '\0';
    return n;
}

static int home_read_active_runtime_app(char *dst, rt_size_t cap)
{
    char *p;
    int result = home_read_text_prefix(HUANGSHAN_RUNTIME_ACTIVE_APP_FILE, dst, cap);
    if (result < 0) return result;
    for (p = dst; *p; p++)
    {
        if (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t')
        {
            *p = '\0';
            break;
        }
    }
    return RT_EOK;
}

static int home_json_copy_string(const char *json, const char *key, char *dst, rt_size_t cap)
{
    const char *hit;
    const char *colon;
    const char *open;
    const char *p;
    char needle[40];
    rt_size_t len = 0;
    if (!json || !key || !dst || cap == 0) return 0;
    rt_snprintf(needle, sizeof(needle), "\"%s\"", key);
    hit = strstr(json, needle);
    if (!hit) return 0;
    colon = strchr(hit + strlen(needle), ':');
    if (!colon) return 0;
    open = strchr(colon, '\"');
    if (!open) return 0;
    p = open + 1;
    while (*p && *p != '\"' && len + 1 < cap)
    {
        if (*p == '\\' && p[1]) p++;
        dst[len++] = *p++;
    }
    dst[len] = '\0';
    return len > 0;
}

static int home_json_copy_string_array(const char *json, const char *key, char *dst, rt_size_t cap)
{
    const char *hit;
    const char *colon;
    const char *p;
    char needle[40];
    rt_size_t len = 0;
    int first = 1;
    if (!json || !key || !dst || cap == 0) return 0;
    rt_snprintf(needle, sizeof(needle), "\"%s\"", key);
    hit = strstr(json, needle);
    if (!hit) return 0;
    colon = strchr(hit + strlen(needle), ':');
    if (!colon) return 0;
    p = strchr(colon, '[');
    if (!p) return 0;
    p++;
    while (*p && *p != ']')
    {
        const char *open;
        open = strchr(p, '\"');
        if (!open) break;
        p = open + 1;
        if (!first)
        {
            if (len + 2 >= cap) break;
            dst[len++] = ',';
            dst[len++] = ' ';
        }
        while (*p && *p != '\"' && len + 1 < cap)
        {
            if (*p == '\\' && p[1]) p++;
            dst[len++] = *p++;
        }
        first = 0;
        if (*p == '\"') p++;
    }
    dst[len] = '\0';
    return len > 0;
}

static int home_info_copy_value(const char *text, const char *key, char *dst, rt_size_t cap)
{
    const char *p;
    rt_size_t key_len;
    rt_size_t len = 0;
    if (!text || !key || !dst || cap == 0) return 0;
    key_len = strlen(key);
    p = text;
    while (*p)
    {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=')
        {
            p += key_len + 1;
            while (*p && *p != '\r' && *p != '\n' && len + 1 < cap) dst[len++] = *p++;
            dst[len] = '\0';
            return len > 0;
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return 0;
}

static uint32_t home_card_color_for_index(int index)
{
    static const uint32_t colors[] =
    {
        0x0f766e, 0x1d4ed8, 0xb45309, 0xbe123c,
        0x047857, 0x7c3aed, 0x0369a1, 0xc2410c,
        0x475569, 0x15803d, 0x4338ca, 0xa21caf,
    };
    return colors[index % (int)(sizeof(colors) / sizeof(colors[0]))];
}

static void home_build_app_path(char *dst, rt_size_t cap, const char *app_id, const char *leaf)
{
    rt_snprintf(dst, cap, "%s/%s/%s", HUANGSHAN_RUNTIME_APP_ROOT, app_id, leaf);
}

static int home_load_runtime_app_card(const char *app_id, huangshan_home_card_t *card, int index)
{
    char path[180];
    char text[1024];
    int has_manifest;
    int has_info;
    if (!home_is_safe_app_id(app_id) || !card) return 0;
    home_build_app_path(path, sizeof(path), app_id, "main.lua");
    if (!home_file_exists(path)) return 0;

    memset(card, 0, sizeof(*card));
    card->kind = HUANGSHAN_HOME_CARD_RUNTIME_APP;
    card->color = home_card_color_for_index(index);
    home_safe_copy(card->target, sizeof(card->target), app_id, app_id);
    home_safe_copy(card->title, sizeof(card->title), app_id, app_id);
    home_safe_copy(card->subtitle, sizeof(card->subtitle), "ready", "ready");
    home_safe_copy(card->category, sizeof(card->category), "General", "General");
    home_safe_copy(card->author, sizeof(card->author), "Unknown", "Unknown");
    home_safe_copy(card->requirements, sizeof(card->requirements), "Runtime", "Runtime");

    home_build_app_path(path, sizeof(path), app_id, "manifest.json");
    has_manifest = home_read_text_prefix(path, text, sizeof(text)) > 0;
    if (has_manifest)
    {
        (void)home_json_copy_string(text, "name", card->title, sizeof(card->title));
        if (!home_json_copy_string(text, "description", card->subtitle, sizeof(card->subtitle)))
        {
            home_safe_copy(card->subtitle, sizeof(card->subtitle), "Runtime app", "Runtime app");
        }
        (void)home_json_copy_string(text, "category", card->category, sizeof(card->category));
        (void)home_json_copy_string(text, "author", card->author, sizeof(card->author));
        if (!home_json_copy_string_array(text, "requirements", card->requirements, sizeof(card->requirements)))
        {
            (void)home_json_copy_string(text, "requirements", card->requirements, sizeof(card->requirements));
        }
        return 1;
    }

    home_build_app_path(path, sizeof(path), app_id, "app.info");
    has_info = home_read_text_prefix(path, text, sizeof(text)) > 0;
    if (has_info)
    {
        (void)home_info_copy_value(text, "name", card->title, sizeof(card->title));
        if (!home_info_copy_value(text, "description", card->subtitle, sizeof(card->subtitle)))
        {
            home_safe_copy(card->subtitle, sizeof(card->subtitle), "Runtime app", "Runtime app");
        }
        (void)home_info_copy_value(text, "category", card->category, sizeof(card->category));
        (void)home_info_copy_value(text, "author", card->author, sizeof(card->author));
        (void)home_info_copy_value(text, "requirements", card->requirements, sizeof(card->requirements));
    }
    return has_manifest || has_info;
}

static void home_sort_cards(void)
{
    int i;
    int j;
    for (i = 0; i < huangshan_home_card_count; i++)
    {
        for (j = i + 1; j < huangshan_home_card_count; j++)
        {
            if (strcmp(huangshan_home_cards[i].title, huangshan_home_cards[j].title) > 0)
            {
                huangshan_home_card_t tmp = huangshan_home_cards[i];
                huangshan_home_cards[i] = huangshan_home_cards[j];
                huangshan_home_cards[j] = tmp;
            }
        }
    }
}

static int home_load_installed_runtime_apps(void)
{
    DIR *dir;
    struct dirent *entry;
    huangshan_home_card_count = 0;
    dir = opendir(HUANGSHAN_RUNTIME_APP_ROOT);
    if (!dir) return -RT_ERROR;
    while ((entry = readdir(dir)) != RT_NULL)
    {
        if (entry->d_name[0] == '.') continue;
        if (huangshan_home_card_count >= HUANGSHAN_HOME_MAX_APPS) break;
        if (home_load_runtime_app_card(entry->d_name,
                                       &huangshan_home_cards[huangshan_home_card_count],
                                       huangshan_home_card_count))
        {
            huangshan_home_card_count++;
        }
    }
    closedir(dir);
    home_sort_cards();
    return RT_EOK;
}

static int home_count_installed_runtime_apps(void)
{
    DIR *dir;
    struct dirent *entry;
    int count = 0;
    dir = opendir(HUANGSHAN_RUNTIME_APP_ROOT);
    if (!dir) return -1;
    while ((entry = readdir(dir)) != RT_NULL)
    {
        char path[180];
        if (entry->d_name[0] == '.' || !home_is_safe_app_id(entry->d_name)) continue;
        home_build_app_path(path, sizeof(path), entry->d_name, "main.lua");
        if (home_file_exists(path)) count++;
    }
    closedir(dir);
    return count;
}

static void home_close_detail(void)
{
    if (huangshan_home_detail)
    {
        lv_obj_del(huangshan_home_detail);
        huangshan_home_detail = RT_NULL;
    }
    huangshan_home_pending_delete = RT_NULL;
    huangshan_home_detail_tracking = 0;
}

static void home_rebuild_ui(void)
{
    if (huangshan_home_root)
    {
        lv_obj_del(huangshan_home_root);
        huangshan_home_root = NULL;
        huangshan_home_status = NULL;
        huangshan_home_list = NULL;
    }
    huangshan_home_ui_init();
}

static void home_refresh_timer_cb(lv_timer_t *timer)
{
    int installed;
    (void)timer;
    if (!huangshan_home_root || huangshan_home_detail) return;
    installed = home_count_installed_runtime_apps();
    if (installed >= 0 && installed != huangshan_home_card_count)
    {
        rt_kprintf("[Huangshan_Home] app count changed %d -> %d\n",
                   huangshan_home_card_count, installed);
        home_rebuild_ui();
    }
}

static void home_start_runtime_card(const huangshan_home_card_t *card)
{
    int result;
    if (!card || !card->target[0]) return;
    result = home_write_active_runtime_app(card->target);
    if (result != RT_EOK)
    {
        home_set_status("SD card app not ready", lv_color_hex(0xfca5a5));
        rt_kprintf("[Huangshan_Home] select runtime app failed %s rc=%d\n", card->target, result);
        return;
    }
    home_set_status("starting Runtime app", lv_color_hex(0xa7f3d0));
    rt_kprintf("[Huangshan_Home] runtime target %s\n", card->target);
    gui_app_run("vb_runtime");
}

static void home_detail_edge_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev;
    lv_point_t point = {0, 0};
    lv_coord_t dx;
    lv_coord_t dy;
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST &&
        code != LV_EVENT_GESTURE)
    {
        return;
    }
    indev = lv_event_get_indev(event);
    if (!indev) indev = lv_indev_get_act();
    if (indev) lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED)
    {
        huangshan_home_detail_press = point;
        huangshan_home_detail_tracking = 1;
        return;
    }
    if (!huangshan_home_detail_tracking) return;
    dx = point.x - huangshan_home_detail_press.x;
    dy = point.y - huangshan_home_detail_press.y;
    if (dx >= HUANGSHAN_HOME_DETAIL_BACK_DX &&
        LV_ABS(dy) <= HUANGSHAN_HOME_DETAIL_BACK_MAX_DY)
    {
        rt_kprintf("[Huangshan_Home] details edge back dx=%d dy=%d\n", (int)dx, (int)dy);
        home_close_detail();
        home_set_status("details closed", lv_color_hex(0xa8b3bd));
        return;
    }
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST || code == LV_EVENT_GESTURE)
    {
        huangshan_home_detail_tracking = 0;
    }
}

static void home_detail_launch_event_cb(lv_event_t *event)
{
    const huangshan_home_card_t *card = (const huangshan_home_card_t *)lv_event_get_user_data(event);
    if (LV_EVENT_CLICKED != lv_event_get_code(event) || !card) return;
    home_start_runtime_card(card);
}

static void home_detail_delete_event_cb(lv_event_t *event)
{
    const huangshan_home_card_t *card = (const huangshan_home_card_t *)lv_event_get_user_data(event);
    char active[GUI_APP_CMD_MAX_LEN];
    char path[180];
    int was_active = 0;
    if (LV_EVENT_CLICKED != lv_event_get_code(event) || !card || !card->target[0]) return;
    if (huangshan_home_pending_delete != card)
    {
        huangshan_home_pending_delete = card;
        home_set_status("tap Delete again to confirm", lv_color_hex(0xfbbf24));
        return;
    }
    if (home_read_active_runtime_app(active, sizeof(active)) == RT_EOK && strcmp(active, card->target) == 0)
    {
        was_active = 1;
    }
    home_build_app_path(path, sizeof(path), card->target, "");
    if (path[0] && path[strlen(path) - 1] == '/') path[strlen(path) - 1] = '\0';
    if (home_remove_tree(path) != RT_EOK)
    {
        home_set_status("delete failed", lv_color_hex(0xfca5a5));
        rt_kprintf("[Huangshan_Home] delete runtime app failed %s\n", card->target);
        return;
    }
    if (was_active) (void)home_write_active_runtime_app("welcome");
    home_close_detail();
    home_set_status("app deleted", lv_color_hex(0xa7f3d0));
    rt_kprintf("[Huangshan_Home] deleted runtime app %s\n", card->target);
    home_rebuild_ui();
}

static void home_show_detail(const huangshan_home_card_t *card)
{
    char line[160];
    const lv_coord_t safe_x = HUANGSHAN_HOME_SAFE_LEFT;
    const lv_coord_t safe_y = HUANGSHAN_HOME_SAFE_TOP;
    const lv_coord_t safe_w = LV_HOR_RES_MAX - HUANGSHAN_HOME_SAFE_LEFT - HUANGSHAN_HOME_SAFE_RIGHT;
    if (!card) return;
    home_close_detail();
    huangshan_home_detail = lv_obj_create(lv_layer_top());
    lv_obj_set_size(huangshan_home_detail, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_align(huangshan_home_detail, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(huangshan_home_detail, LV_OBJ_FLAG_SCROLLABLE);
    home_set_obj_bg(huangshan_home_detail, 0x07111f);

    lv_obj_t *title = home_create_label(huangshan_home_detail, card->title, FONT_BIGL, LV_COLOR_WHITE);
    lv_obj_set_width(title, safe_w);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, safe_y + 18);

    rt_snprintf(line, sizeof(line), "%s  by %s", card->category, card->author);
    lv_obj_t *meta = home_create_label(huangshan_home_detail, line, FONT_SMALL, lv_color_hex(0x93c5fd));
    lv_obj_set_width(meta, safe_w);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(meta, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(meta, LV_ALIGN_TOP_MID, 0, safe_y + 60);

    lv_obj_t *desc = home_create_label(huangshan_home_detail, card->subtitle, FONT_SMALL, lv_color_hex(0xd7e5f5));
    lv_obj_set_width(desc, safe_w);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(desc, LV_ALIGN_TOP_MID, 0, safe_y + 94);

    rt_snprintf(line, sizeof(line), "Needs: %s", card->requirements);
    lv_obj_t *req = home_create_label(huangshan_home_detail, line, FONT_SMALL, lv_color_hex(0xa7f3d0));
    lv_obj_set_width(req, safe_w);
    lv_label_set_long_mode(req, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(req, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(req, LV_ALIGN_TOP_MID, 0, safe_y + 158);

    lv_obj_t *launch = lv_btn_create(huangshan_home_detail);
    lv_obj_set_size(launch, 142, 40);
    lv_obj_align(launch, LV_ALIGN_BOTTOM_LEFT, safe_x + 32, -HUANGSHAN_HOME_SAFE_BOTTOM);
    lv_obj_set_style_radius(launch, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(launch, lv_color_hex(0x2dd4bf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(launch, home_detail_launch_event_cb, LV_EVENT_CLICKED, (void *)card);
    lv_obj_t *launch_label = home_create_label(launch, "Launch", FONT_SMALL, lv_color_hex(0x0f172a));
    lv_obj_center(launch_label);

    lv_obj_t *del = lv_btn_create(huangshan_home_detail);
    lv_obj_set_size(del, 96, 40);
    lv_obj_align(del, LV_ALIGN_BOTTOM_RIGHT, -safe_x, -HUANGSHAN_HOME_SAFE_BOTTOM);
    lv_obj_set_style_radius(del, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(del, lv_color_hex(0x7f1d1d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(del, home_detail_delete_event_cb, LV_EVENT_CLICKED, (void *)card);
    lv_obj_t *del_label = home_create_label(del, "Delete", FONT_SMALL, LV_COLOR_WHITE);
    lv_obj_center(del_label);

    lv_obj_t *edge = lv_obj_create(huangshan_home_detail);
    lv_obj_remove_style_all(edge);
    lv_obj_set_size(edge, HUANGSHAN_HOME_DETAIL_EDGE_WIDTH, LV_VER_RES_MAX);
    lv_obj_align(edge, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(edge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(edge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN |
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_opa(edge, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_ext_click_area(edge, 8);
    lv_obj_add_event_cb(edge, home_detail_edge_event_cb, LV_EVENT_PRESSED, RT_NULL);
    lv_obj_add_event_cb(edge, home_detail_edge_event_cb, LV_EVENT_PRESSING, RT_NULL);
    lv_obj_add_event_cb(edge, home_detail_edge_event_cb, LV_EVENT_RELEASED, RT_NULL);
    lv_obj_add_event_cb(edge, home_detail_edge_event_cb, LV_EVENT_PRESS_LOST, RT_NULL);
    lv_obj_add_event_cb(edge, home_detail_edge_event_cb, LV_EVENT_GESTURE, RT_NULL);

    home_set_status("details", lv_color_hex(0xa8b3bd));
}

static void home_card_event_cb(lv_event_t *event)
{
    const huangshan_home_card_t *card = (const huangshan_home_card_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);

    if ((code != LV_EVENT_SHORT_CLICKED && code != LV_EVENT_LONG_PRESSED) ||
        !card || !card->target[0])
    {
        return;
    }

    if (card->kind == HUANGSHAN_HOME_CARD_RUNTIME_APP)
    {
        if (code == LV_EVENT_LONG_PRESSED)
        {
            home_show_detail(card);
        }
        else
        {
            home_start_runtime_card(card);
        }
        return;
    }

    if (code != LV_EVENT_SHORT_CLICKED) return;

    home_set_status("opening app", lv_color_hex(0xa7f3d0));
    rt_kprintf("[Huangshan_Home] run %s\n", card->target);
    gui_app_run(card->target);
}

static lv_obj_t *home_create_card(lv_obj_t *parent, const huangshan_home_card_t *card)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, card->w, card->h);
    lv_obj_align(obj, LV_ALIGN_TOP_LEFT, card->x, card->y);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    home_set_obj_bg(obj, card->color);
    lv_obj_set_style_radius(obj, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x334155), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(obj, home_card_event_cb, LV_EVENT_SHORT_CLICKED, (void *)card);
    lv_obj_add_event_cb(obj, home_card_event_cb, LV_EVENT_LONG_PRESSED, (void *)card);

    lv_obj_t *title = home_create_label(obj, card->title, FONT_SMALL, LV_COLOR_WHITE);
    lv_obj_set_width(title, card->w - 18);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 9, 7);

    lv_obj_t *subtitle = home_create_label(obj, card->subtitle, FONT_SMALL, lv_color_hex(0xd7e5f5));
    lv_obj_set_width(subtitle, card->w - 18);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_CLIP);
    lv_obj_align(subtitle, LV_ALIGN_BOTTOM_LEFT, 9, -7);

    return obj;
}

static void home_create_empty_state(lv_obj_t *parent, lv_coord_t width)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, width, 118);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 0);
    home_set_obj_bg(box, 0x162235);
    lv_obj_set_style_radius(box, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(box, lv_color_hex(0x334155), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = home_create_label(box, "No installed apps", FONT_NORMAL, LV_COLOR_WHITE);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *hint = home_create_label(box, "Install from the computer App Store", FONT_SMALL, lv_color_hex(0xbfdbfe));
    lv_obj_set_width(hint, width - 24);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 62);
}

static void huangshan_home_ui_init(void)
{
    const lv_coord_t safe_x = HUANGSHAN_HOME_SAFE_LEFT;
    const lv_coord_t safe_y = HUANGSHAN_HOME_SAFE_TOP;
    const lv_coord_t safe_w = LV_HOR_RES_MAX - HUANGSHAN_HOME_SAFE_LEFT - HUANGSHAN_HOME_SAFE_RIGHT;
    const lv_coord_t safe_h = LV_VER_RES_MAX - HUANGSHAN_HOME_SAFE_TOP - HUANGSHAN_HOME_SAFE_BOTTOM;
    const lv_coord_t card_gap = 10;
    const lv_coord_t card_w = (safe_w - card_gap) / 2;
    const lv_coord_t card_h = 62;
    const lv_coord_t list_y = safe_y + 88;
    const lv_coord_t status_h = 24;
    const lv_coord_t list_h = safe_h - 88 - status_h;
    uint32_t i;
    int load_result;

    huangshan_home_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(huangshan_home_root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_align(huangshan_home_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(huangshan_home_root, LV_OBJ_FLAG_SCROLLABLE);
    home_set_obj_bg(huangshan_home_root, 0x07111f);

    lv_obj_t *title = home_create_label(huangshan_home_root, "Huangshan Pi", FONT_BIGL, LV_COLOR_WHITE);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, safe_x, safe_y);

    lv_obj_t *subtitle = home_create_label(huangshan_home_root, "Installed Runtime Apps",
                                           FONT_SMALL, lv_color_hex(0x93c5fd));
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, safe_x, safe_y + 38);

    lv_obj_t *panel = lv_obj_create(huangshan_home_root);
    lv_obj_set_size(panel, 92, 42);
    lv_obj_align(panel, LV_ALIGN_TOP_RIGHT, -safe_x, safe_y + 2);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    home_set_obj_bg(panel, 0x162235);
    lv_obj_set_style_radius(panel, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *panel_text = home_create_label(panel, "BLE / SD", FONT_SMALL, lv_color_hex(0xe0f2fe));
    lv_obj_center(panel_text);

    huangshan_home_list = lv_obj_create(huangshan_home_root);
    lv_obj_set_size(huangshan_home_list, safe_w, list_h > 120 ? list_h : 120);
    lv_obj_align(huangshan_home_list, LV_ALIGN_TOP_LEFT, safe_x, list_y);
    lv_obj_set_style_bg_opa(huangshan_home_list, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(huangshan_home_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(huangshan_home_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(huangshan_home_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scroll_dir(huangshan_home_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(huangshan_home_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(huangshan_home_list, LV_OBJ_FLAG_SCROLLABLE |
                    LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);

    load_result = home_load_installed_runtime_apps();
    if (load_result == RT_EOK && huangshan_home_card_count > 0)
    {
        for (i = 0; i < (uint32_t)huangshan_home_card_count; i++)
        {
            lv_coord_t col = i % 2;
            lv_coord_t row = i / 2;
            huangshan_home_cards[i].x = col * (card_w + card_gap);
            huangshan_home_cards[i].y = row * (card_h + card_gap);
            huangshan_home_cards[i].w = card_w;
            huangshan_home_cards[i].h = card_h;
            home_create_card(huangshan_home_list, &huangshan_home_cards[i]);
        }
        {
            lv_coord_t rows = (huangshan_home_card_count + 1) / 2;
            lv_coord_t content_h = rows * (card_h + card_gap) - card_gap + 12;
            lv_obj_t *extent = lv_obj_create(huangshan_home_list);
            lv_obj_remove_style_all(extent);
            lv_obj_set_size(extent, 1, 1);
            lv_obj_set_pos(extent, 0, content_h - 1);
            lv_obj_clear_flag(extent, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                              LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
        }
    }
    else
    {
        home_create_empty_state(huangshan_home_list, safe_w);
    }

    huangshan_home_status = home_create_label(huangshan_home_root, "",
                                              FONT_SMALL, lv_color_hex(0xa8b3bd));
    lv_obj_set_width(huangshan_home_status, safe_w);
    lv_obj_set_style_text_align(huangshan_home_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(huangshan_home_status, LV_ALIGN_TOP_MID, 0, safe_y + safe_h - 22);
    if (load_result == RT_EOK)
    {
        char status[64];
        rt_snprintf(status, sizeof(status), "%d installed apps", huangshan_home_card_count);
        home_set_status(status, lv_color_hex(0xa8b3bd));
    }
    else
    {
        home_set_status("Insert SD card to show apps", lv_color_hex(0xfca5a5));
    }

    rt_kprintf("[Huangshan_Home] apps=%d safe=%d,%d,%d,%d\n",
               huangshan_home_card_count, (int)safe_x, (int)safe_y, (int)safe_w, (int)safe_h);
}

static void on_start(void)
{
    memset(&app_mainmenu_ctx, 0, sizeof(app_mainmenu_ctx));
    huangshan_home_refresh_on_resume = 0;
    huangshan_home_ui_init();
    huangshan_home_refresh_timer = lv_timer_create(home_refresh_timer_cb,
                                                   HUANGSHAN_HOME_REFRESH_MS, RT_NULL);
}

static void on_resume(void)
{
    if (huangshan_home_refresh_on_resume)
    {
        home_close_detail();
        home_rebuild_ui();
        huangshan_home_refresh_on_resume = 0;
    }
}
static void on_pause(void)
{
    huangshan_home_refresh_on_resume = 1;
    mm_trans_anim_init();
#ifdef AUTO_CIRCLE_ANIM
    if (app_mainmenu_ctx.pg_obj)
    {
        lv_anim_del(app_mainmenu_ctx.pg_obj, app_mainmenu_auto_circle);
    }
#endif

    if (app_mainmenu_ctx.anim_obj)
    {
        lv_anim_del(app_mainmenu_ctx.anim_obj, NULL);
        app_mainmenu_ctx.anim_obj = NULL;
    }

}
static void on_stop(void)
{
    if (huangshan_home_refresh_timer)
    {
        lv_timer_del(huangshan_home_refresh_timer);
        huangshan_home_refresh_timer = RT_NULL;
    }
    home_close_detail();
    if (huangshan_home_root)
    {
        lv_obj_del(huangshan_home_root);
        huangshan_home_root = NULL;
        huangshan_home_status = NULL;
        huangshan_home_list = NULL;
    }

    if (app_mainmenu_ctx.list)
    {
        rt_free(app_mainmenu_ctx.list);
        app_mainmenu_ctx.list = NULL;
    }
#ifdef DEBUG_APP_MAINMENU_DISPLAY_ICON_COORDINATE
    if (app_mainmenu_ctx.label_list)
    {
        rt_free(app_mainmenu_ctx.label_list);
        app_mainmenu_ctx.label_list = NULL;
    }
#endif
    if (app_mainmenu_ctx.icon_pivot)
    {
        rt_free(app_mainmenu_ctx.icon_pivot);
        app_mainmenu_ctx.icon_pivot = NULL;
    }
}



static void msg_handler(gui_app_msg_type_t msg, void *param)
{
    switch (msg)
    {
    case GUI_APP_MSG_ONSTART:
        on_start();
        break;

    case GUI_APP_MSG_ONRESUME:
        on_resume();
        break;

    case GUI_APP_MSG_ONPAUSE:
        on_pause();
        break;

    case GUI_APP_MSG_ONSTOP:
        on_stop();
        break;
    default:
        break;
    }
}


static int app_mainmenu(intent_t i)
{
    gui_app_regist_msg_handler(APP_ID, msg_handler);

    return 0;
}



BUILTIN_APP_EXPORT(LV_EXT_STR_ID(mainmenu), NULL, APP_ID, app_mainmenu);
