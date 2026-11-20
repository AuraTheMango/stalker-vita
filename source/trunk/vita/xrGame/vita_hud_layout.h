#pragma once
#include <array>
// Pixel coordinates in the native 960 x 544 UI, scaled around an inset corner.
inline std::array<float,4> vita_hud_rect(float x,float y,float w,float h,float scale,float ax,float ay)
{
 return {ax+(x-ax)*scale,ay+(y-ay)*scale,w*scale,h*scale};
}
struct VitaProfileLayout
{
 float x,y,width;
 float scale()const{return width/210.f;}
 std::array<float,4> rect(float rx,float ry,float w,float h)const{
  const float s=scale();return {x+rx*s,y+ry*s,w*s,h*s};
 }
 std::array<float,4> image(bool right)const{return rect(right?20.f:32.f,35,165,108);}
 std::array<float,4> name(bool right)const{return rect(right?20.f:32.f,7,165,22);}
 // Only the portrait card: the empty reputation fields below are not used.
 std::array<float,4> frame()const{return rect(0,0,210,150);}
};
