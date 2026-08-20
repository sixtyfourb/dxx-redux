/*
 * Stub implementation of xmodel functions for Android/GLES builds.
 * xmodel requires GLEW and GL 2.0+ features (VBOs) not available in GLES 1.x.
 * All functions are no-ops; xmodel_show_if_loaded returns 0 so the engine
 * falls back to normal polygon model rendering.
 */

#include "xmodel.h"

void *xmodel_load(const char *filename) { return 0; }
void xmodel_free(void *model) {}
int xmodel_load_gl(void *model) { return 0; }
void xmodel_free_gl(void *model) {}
void xmodel_show(void *model, int team, g3s_lrgb *light) {}
void xmodel_show_at(void *model, vms_vector *pos, vms_matrix *orient, int team, g3s_lrgb *light) {}
void xmodel_load_all() {}
void xmodel_free_all() {}
void xmodel_load_gl_all() {}
void xmodel_free_gl_all() {}
int xmodel_show_if_loaded(int modelnum, vms_vector *pos, vms_matrix *orient, int mpcolor, g3s_lrgb *light) { return 0; }
