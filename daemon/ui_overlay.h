#ifndef UI_OVERLAY_H_INCLUDED
#define UI_OVERLAY_H_INCLUDED

int ui_overlay_valid(void);
int ui_overlay_refresh(void);
void ui_overlay_blend(unsigned char *nv12, int w, int h);

#endif