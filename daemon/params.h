#ifndef PARAMS_H_INCLUDED
#define PARAMS_H_INCLUDED

struct cam_params {
    int pid;
    int iso;
    int iso_max;
    unsigned int shutter_num;
    unsigned int shutter_den;
    unsigned int aperture10;
    unsigned int tv16;
    unsigned int av16;
};

int params_read(struct cam_params *p);

#endif