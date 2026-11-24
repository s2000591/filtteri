// Hilbert 90-degree check + stability torture for AvaruusradioFilter.
// Build: g++ -std=c++17 -O2 -Iinclude tests/avaruusradio_test.cpp -o avaruusradio_test
#include "AvaruusradioFilter.h"
#include <cstdio>
#include <random>
int main(){
  const double FS=48000;
  // Hilbert 90° check
  for(double f: {50.0,100.0,500.0,2000.0,8000.0,15000.0}){
    kh::ar::Hilbert h; h.reset(); double sre=0,sim=0,cross=0;
    for(int i=0;i<48000;i++){double x=std::sin(2*kh::ar::kPi*f*i/FS),re,im;h.tick(x,re,im);
      if(i>24000){sre+=re*re;sim+=im*im;cross+=re*im;}}
    printf("f=%6.0f  amp ratio %.3f  corr %.4f (0 = 90deg)\n",f,std::sqrt(sim/sre),cross/std::sqrt(sre*sim));
  }
  std::mt19937 rng(3); std::uniform_real_distribution<double> U(0,1);
  double worst=0; bool nan=false;
  for(int t=0;t<200;t++){
    kh::AvaruusradioFilter f; f.prepare(FS); f.setSeed(t+1);
    double amp=0.1+3*U(rng), ph=0, fr=50+500*U(rng);
    for(int i=0;i<48000;i++){
      if(i%2000==0){f.setBabble(U(rng));f.setRadio(U(rng));f.setVowel(U(rng));f.setRate(1+15*U(rng));f.setSharpness(U(rng));}
      ph+=fr/FS; if(ph>1)ph-=1;
      float y=f.process((float)(amp*((2*ph-1)*0.7+0.3*(U(rng)-0.5))));
      if(!std::isfinite(y)){nan=true;} worst=std::max(worst,(double)std::abs(y));
    }
  }
  printf("stability: nan=%d worst=%.2f\n",nan,worst);
  return nan;
}
