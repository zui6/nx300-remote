#include <stdio.h>
#include <stdlib.h>
#include "params.h"

int main(void)
{
    struct cam_params p;
    fprintf(stderr, "step1: calling params_read\n");
    if (params_read(&p) != 0) {
        fprintf(stderr, "params_read FAILED\n");
        return 1;
    }
    fprintf(stderr, "step2: pid=%d\n", p.pid);
    printf("iso=%d iso_max=%d shutter=%u/%u aperture=%u tv16=%u av16=%u\n",
           p.iso, p.iso_max, p.shutter_num, p.shutter_den,
           p.aperture10, p.tv16, p.av16);
    return 0;
}