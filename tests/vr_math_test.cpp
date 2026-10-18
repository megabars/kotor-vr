// Standalone regression executable using the production matrix routines.
// Compile this file (not vr_backend.cpp separately); no XR runtime is needed.
#define KVR_MATH_TEST_ONLY
#include "../src/vr_backend.cpp"
#include "../src/cursor_math.h"
#include <cstdlib>

namespace {
void expectNear(float actual,float expected,const char* what) {
    if (std::fabs(actual-expected)>0.0001f) {
        std::fprintf(stderr,"%s: actual=%f expected=%f\n",what,actual,expected);
        std::exit(1);
    }
}
void transform(const float* m,float x,float y,float z,float* out) {
    for (int row=0;row<4;++row) out[row]=m[row]*x+m[4+row]*y+m[8+row]*z+m[12+row];
}
}
int main() {
    float m[16],point[4];
    XrPosef identity{{0,0,0,1},{0,0,0}};
    kvr::inversePoseMatrix(identity,1,m);
    transform(m,1,2,-3,point);
    expectNear(point[0],1,"identity x"); expectNear(point[1],2,"identity y"); expectNear(point[2],-3,"identity z");

    // Positive eye position produces the opposite camera translation.
    XrPosef rightEye{{0,0,0,1},{0.032f,0,0}};
    kvr::inversePoseMatrix(rightEye,2,m);
    expectNear(m[12],-0.064f,"eye separation and world scale");

    // A 90-degree right turn of the camera sees world +X straight ahead.
    const float halfSqrt=std::sqrt(0.5f);
    XrPosef turned{{0,-halfSqrt,0,halfSqrt},{2,3,4}};
    kvr::inversePoseMatrix(turned,1,m);
    transform(m,3,3,4,point);
    expectNear(point[0],0,"rotated view x"); expectNear(point[1],0,"rotated view y"); expectNear(point[2],-1,"rotated view forward");

    // An asymmetric frustum must map both edges and clip planes to NDC +-1.
    XrFovf fov{-0.7f,0.9f,0.8f,-0.6f};
    const float nearZ=0.1f,farZ=10000;
    kvr::projectionMatrix(fov,nearZ,farZ,m);
    transform(m,std::tan(fov.angleLeft)*nearZ,0,-nearZ,point);
    expectNear(point[0]/point[3],-1,"left frustum edge"); expectNear(point[2]/point[3],-1,"near clip");
    transform(m,std::tan(fov.angleRight)*nearZ,0,-nearZ,point);
    expectNear(point[0]/point[3],1,"right frustum edge");
    transform(m,0,std::tan(fov.angleUp)*nearZ,-nearZ,point);
    expectNear(point[1]/point[3],1,"top frustum edge");
    transform(m,0,std::tan(fov.angleDown)*nearZ,-nearZ,point);
    expectNear(point[1]/point[3],-1,"bottom frustum edge");
    transform(m,0,0,-farZ,point); expectNear(point[2]/point[3],1,"far clip");

    // Mouse hotspot must reproject to the original game's picking pixel even
    // with an asymmetric projection, before applying each tracked eye pose.
    float cameraPoint[3];
    const float px=217,py=133;const int width=1280,height=960;
    if(!kvr::cursorUnproject(m,px,py,width,height,0.95f,cameraPoint))return 1;
    transform(m,cameraPoint[0],cameraPoint[1],cameraPoint[2],point);
    expectNear((point[0]/point[3]+1)*0.5f*width-0.5f,px,"cursor picking x");
    expectNear((1-point[1]/point[3])*0.5f*height-0.5f,py,"cursor picking y");
    expectNear((point[2]/point[3]+1)*0.5f,0.95f,"cursor surface depth");
    float singular[16]{};
    if(kvr::cursorUnproject(singular,0,0,width,height,0.5f,cameraPoint))return 1;
    if(kvr::cursorUnproject(m,0,0,0,height,0.5f,cameraPoint))return 1;
    std::puts("VR matrix tests passed");
}
