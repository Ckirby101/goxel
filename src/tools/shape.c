/* Goxel 3D voxels editor
 *
 * copyright (c) 2017 Guillaume Chereau <guillaume@noctua-software.com>
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.

 * Goxel is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.

 * You should have received a copy of the GNU General Public License along with
 * goxel.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "goxel.h"

enum {
    STATE_IDLE      = 0,
    STATE_CANCEL    = 1,
    STATE_END       = 2,

    STATE_SNAPED,
    STATE_PAINT,
    STATE_PAINT2,
    STATE_WAIT_UP,
    STATE_WAIT_KEY_UP,

    STATE_ENTER = 0x0100,
};

typedef struct {
    tool_t tool;
    vec3_t start_pos;
    mesh_t *mesh_orig;
} tool_shape_t;


static box_t get_box(const vec3_t *p0, const vec3_t *p1, const vec3_t *n,
                     float r, const plane_t *plane)
{
    mat4_t rot;
    box_t box;
    if (p1 == NULL) {
        box = bbox_from_extents(*p0, r, r, r);
        box = box_swap_axis(box, 2, 0, 1);
        return box;
    }
    if (r == 0) {
        box = bbox_grow(bbox_from_points(*p0, *p1), 0.5, 0.5, 0.5);
        // Apply the plane rotation.
        rot = plane->mat;
        rot.vecs[3] = vec4(0, 0, 0, 1);
        mat4_imul(&box.mat, rot);
        return box;
    }

    // Create a box for a line:
    int i;
    const vec3_t AXES[] = {vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)};

    box.mat = mat4_identity;
    box.p = vec3_mix(*p0, *p1, 0.5);
    box.d = vec3_sub(*p1, box.p);
    for (i = 0; i < 3; i++) {
        box.w = vec3_cross(box.d, AXES[i]);
        if (vec3_norm2(box.w) > 0) break;
    }
    if (i == 3) return box;
    box.w = vec3_mul(vec3_normalized(box.w), r);
    box.h = vec3_mul(vec3_normalized(vec3_cross(box.d, box.w)), r);
    return box;
}

static int iter(tool_t *tool, const vec4_t *view)
{
    tool_shape_t *shape = (tool_shape_t*)tool;
    box_t box;
    uvec4b_t box_color = HEXCOLOR(0xffff00ff);
    mesh_t *mesh = goxel->image->active_layer->mesh;
    cursor_t *curs = &goxel->cursor;
    curs->snap_offset = 0.5;

    if (!shape->mesh_orig)
        shape->mesh_orig = mesh_copy(goxel->image->active_layer->mesh);

    switch (tool->state) {

    case STATE_IDLE:
        if (curs->snaped) return STATE_SNAPED;
        break;

    case STATE_SNAPED | STATE_ENTER:
        mesh_set(shape->mesh_orig, mesh);
        break;

    case STATE_SNAPED:
        if (!curs->snaped) return STATE_CANCEL;
        goxel_set_help_text(goxel, "Click and drag to draw.");
        shape->start_pos = curs->pos;
        box = get_box(&shape->start_pos, &curs->pos, &curs->normal, 0,
                      &goxel->plane);
        render_box(&goxel->rend, &box, &box_color, EFFECT_WIREFRAME);
        if (curs->flags & CURSOR_PRESSED) {
            tool->state = STATE_PAINT;
            image_history_push(goxel->image);
        }
        break;

    case STATE_PAINT:
        goxel_set_help_text(goxel, "Drag.");
        if (!curs->snaped) return tool->state;
        box = get_box(&shape->start_pos, &curs->pos, &curs->normal,
                      0, &goxel->plane);
        render_box(&goxel->rend, &box, &box_color, EFFECT_WIREFRAME);
        mesh_set(mesh, shape->mesh_orig);
        mesh_op(mesh, &goxel->painter, &box);
        goxel_update_meshes(goxel, MESH_LAYERS);
        if (!(curs->flags & CURSOR_PRESSED)) {
            if (!goxel->tool_shape_two_steps) {
                goxel_update_meshes(goxel, -1);
                tool->state = STATE_END;
            } else {
                goxel->tool_plane = plane_from_normal(curs->pos,
                                                      goxel->plane.u);
                tool->state = STATE_PAINT2;
            }
        }
        break;

    case STATE_PAINT2:
        goxel_set_help_text(goxel, "Adjust height.");
        if (!curs->snaped) return tool->state;
        // XXX: clean this up...
        curs->pos = vec3_add(goxel->tool_plane.p,
                       vec3_project(vec3_sub(curs->pos, goxel->tool_plane.p),
                                    goxel->plane.n));
        curs->pos.x = round(curs->pos.x - 0.5) + 0.5;
        curs->pos.y = round(curs->pos.y - 0.5) + 0.5;
        curs->pos.z = round(curs->pos.z - 0.5) + 0.5;

        box = get_box(&shape->start_pos, &curs->pos, &curs->normal, 0,
                      &goxel->plane);
        render_box(&goxel->rend, &box, &box_color, EFFECT_WIREFRAME);
        mesh_set(mesh, shape->mesh_orig);
        mesh_op(mesh, &goxel->painter, &box);
        goxel_update_meshes(goxel, MESH_LAYERS);
        if (curs->flags & CURSOR_PRESSED) {
            goxel_update_meshes(goxel, -1);
            return STATE_WAIT_UP;
        }
        break;

    case STATE_WAIT_UP:
        goxel->tool_plane = plane_null;
        if (!(curs->flags & CURSOR_PRESSED)) return STATE_END;
        break;

    }
    return tool->state;
}

static int cancel(tool_t *tool)
{
    if (!tool) return 0;
    tool_shape_t *shape = (tool_shape_t*)tool;
    mesh_set(goxel->image->active_layer->mesh, shape->mesh_orig);
    mesh_delete(shape->mesh_orig);
    shape->mesh_orig = NULL;
    return 0;
}

static int gui(tool_t *tool)
{
    tool_gui_smoothness();
    gui_checkbox("Two steps", &goxel->tool_shape_two_steps,
                 "Second click set the height");
    tool_gui_snap();
    tool_gui_mode();
    tool_gui_shape();
    tool_gui_color();
    if (!box_is_null(goxel->selection))
        gui_action_button("fill_selection", "Fill selection", 1.0, "");

    return 0;
}

TOOL_REGISTER(TOOL_SHAPE, shape, tool_shape_t,
              .iter_fn = iter,
              .cancel_fn = cancel,
              .gui_fn = gui,
              .shortcut = "S",
)