#include "platform/remote_color_conversion.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace RemoteColorConversion;
int main() {
  try {
    for (const auto size : {std::pair<int,int>{2,2}, {34,18}, {1134,712}}) {
      const int w=size.first, h=size.second;
      std::vector<unsigned char> rgba(w*h*4), scalar, fast;
      for (int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const int i=(y*w+x)*4;
        rgba[i]=(x*23+y*7)%256; rgba[i+1]=(x*3+y*31)%256;
        rgba[i+2]=(x*43+y*11)%256; rgba[i+3]=255;
      }
      ScalarRgbaToI420(w,h,rgba,scalar); RgbaToI420(w,h,rgba,fast);
      int maxDifference=0;
      for (size_t i=0;i<fast.size();++i)
        maxDifference=std::max(maxDifference,std::abs(int(fast[i])-int(scalar[i])));
      if(maxDifference>3) throw std::runtime_error("YUV color mismatch");
      // Padded planes exercise decoder strides, not just tightly packed images.
      int ys=w+16, cs=w/2+8;
      std::vector<unsigned char> y(ys*h,0),u(cs*h/2,0),v(cs*h/2,0),a,b;
      for(int row=0;row<h;++row) std::copy_n(scalar.data()+row*w,w,y.data()+row*ys);
      for(int row=0;row<h/2;++row) {
        std::copy_n(scalar.data()+w*h+row*w/2,w/2,u.data()+row*cs);
        std::copy_n(scalar.data()+w*h+w*h/4+row*w/2,w/2,v.data()+row*cs);
      }
      ScalarI420ToRgba(y.data(),u.data(),v.data(),w,h,ys,cs,a);
      I420ToRgba(y.data(),u.data(),v.data(),w,h,ys,cs,b);
      int rgbDifference=0;
      for(size_t i=0;i<a.size();++i) rgbDifference=std::max(rgbDifference,std::abs(int(a[i])-int(b[i])));
      if(rgbDifference>3) throw std::runtime_error("RGBA color/stride mismatch");
      std::cout<<w<<'x'<<h<<" max YUV/RGBA difference "<<maxDifference<<'/'<<rgbDifference<<'\n';
      if(w==1134) {
        auto time=[&](auto fn) {
          const auto start=std::chrono::steady_clock::now();
          for(int n=0;n<100;++n) fn();
          return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100;
        };
        std::cout<<"RGBA->I420 scalar/selected ms "
          <<time([&]{ScalarRgbaToI420(w,h,rgba,scalar);})<<'/'
          <<time([&]{RgbaToI420(w,h,rgba,fast);})<<'\n';
        std::cout<<"I420->RGBA scalar/selected ms "
          <<time([&]{ScalarI420ToRgba(y.data(),u.data(),v.data(),w,h,ys,cs,a);})<<'/'
          <<time([&]{I420ToRgba(y.data(),u.data(),v.data(),w,h,ys,cs,b);})<<'\n';
      }
    }
    return 0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
