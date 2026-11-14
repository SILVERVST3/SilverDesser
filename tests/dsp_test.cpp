#include "../Source/DesserDSP.h"
#include <iostream>
#include <random>
#include <stdexcept>
void require(bool ok) { if (!ok) throw std::runtime_error("DSP regression failed"); }
int main() {
 for (double sr : {44100.,48000.,96000.,192000.}) for(int channels : {1,2}) {
  silver::DesserCore core; core.prepare(sr,channels);
  silver::DesserParams p; p.autoTrack=false; p.ratioDeg=45; core.setParams(p,true);
  int n=18000; std::vector<float> a(n),b(n),ref(n);
  std::mt19937 rng(42); for(int i=0;i<n;i++) ref[i]=a[i]=b[i]=(float(rng()%20001)/10000-1)*.25f;
  for(int i=0;i<n;) {int count=std::min(137,n-i); float* ptr[]={a.data()+i,b.data()+i}; core.process(ptr,channels,count); i+=count;}
  double err=0; int d=core.getLatencySamples(); for(int i=d;i<n;i++) err=std::max(err,double(std::abs(a[i]-ref[i-d])));
  require(err<1e-6); std::cout<<sr<<" Hz / "<<channels<<" ch unity error "<<err<<" latency "<<d<<"\n";
 }
 for(bool wide : {false,true}) {
  silver::DesserCore core; core.prepare(48000,2); silver::DesserParams p;
  p.autoTrack=false;p.audioWide=wide;p.thresholdDb=-30;p.ratioDeg=0;p.attackMs=.01f;core.setParams(p,true);
  std::vector<float>a(48000),b(48000);for(int i=0;i<48000;i++)a[i]=b[i]=.5f*std::sin(2*silver::kPi*7000*i/48000);
  float* ptr[]={a.data(),b.data()};core.process(ptr,2,48000);double power=0;for(int i=24000;i<48000;i++){require(std::isfinite(a[i]));require(a[i]==b[i]);power+=a[i]*a[i];}
  double rms=std::sqrt(power/24000);require(rms<.06);std::cout<<"7kHz limiting wide="<<wide<<" RMS="<<rms<<"\n";
 }
 std::cout<<"PASS\n";
}
