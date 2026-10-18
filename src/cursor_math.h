#pragma once
#include <algorithm>
#include <cmath>

namespace kvr {
// Invert the actual game projection, including asymmetric/orthographic terms.
// This maps the unmodified game's mouse-picking pixel into its camera space.
inline bool cursorUnproject(const float* projection, float pixelX, float pixelY,
                            int width, int height, float depth, float* point) {
    if (!projection || !point || width<=0 || height<=0 || !std::isfinite(depth)) return false;
    double a[4][8]{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c) { a[r][c]=projection[c*4+r];a[r][c+4]=r==c?1.0:0.0; }
    for(int c=0;c<4;++c) {
        int pivot=c;
        for(int r=c+1;r<4;++r)if(std::fabs(a[r][c])>std::fabs(a[pivot][c]))pivot=r;
        if(std::fabs(a[pivot][c])<1e-12)return false;
        for(int k=0;k<8;++k)std::swap(a[c][k],a[pivot][k]);
        const double divisor=a[c][c];for(int k=0;k<8;++k)a[c][k]/=divisor;
        for(int r=0;r<4;++r)if(r!=c) { const double factor=a[r][c];for(int k=0;k<8;++k)a[r][k]-=factor*a[c][k]; }
    }
    const double ndc[4]={2.0*(pixelX+0.5)/width-1.0,1.0-2.0*(pixelY+0.5)/height,2.0*depth-1.0,1.0};
    double result[4]{};
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)result[r]+=a[r][c+4]*ndc[c];
    if(std::fabs(result[3])<1e-12)return false;
    for(int r=0;r<3;++r) { point[r]=float(result[r]/result[3]);if(!std::isfinite(point[r]))return false; }
    return true;
}
}
