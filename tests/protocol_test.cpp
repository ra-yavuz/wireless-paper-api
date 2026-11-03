#include "protocol.h"
#include <assert.h>
#include <vector>
#include <algorithm>
#include <stdio.h>
using namespace paper;
void put16(uint8_t* p,uint16_t v){p[0]=v;p[1]=v>>8;}
void put32(uint8_t* p,uint32_t v){put16(p,v);put16(p+2,v>>16);}
std::vector<std::vector<uint8_t>> encode(const std::vector<uint8_t>& b,uint8_t kind,uint32_t id) {
  std::vector<std::vector<uint8_t>> out;
  for(size_t o=0;o<b.size();o+=CHUNK){size_t n=std::min(CHUNK,b.size()-o);std::vector<uint8_t> p(16+n);p[0]='W';p[1]='P';p[2]=1;p[3]=kind;put32(p.data()+4,id);put16(p.data()+8,b.size());put16(p.data()+10,o);put32(p.data()+12,crc32(b.data(),b.size()));memcpy(p.data()+16,b.data()+o,n);out.push_back(p);}
  return out;
}
int main(){
  assert(crc32((const uint8_t*)"123456789",9)==0xcbf43926);
  for(size_t n:{size_t(1),size_t(224),size_t(225),FRAME_SIZE,MAX_BODY}){
    Assembler a;std::vector<uint8_t> b(n);for(size_t i=0;i<n;i++)b[i]=i*17;
    auto ps=encode(b,n==FRAME_SIZE?2:1,42);
    // Out-of-order assembly, including repeated fragments.
    std::reverse(ps.begin(),ps.end());
    for(size_t i=0;i<ps.size();i++){
      auto& p=ps[i];assert(a.add(p.data(),p.size(),10+i)==(i+1==ps.size()?Result::Complete:Result::Pending));
      if(i+1<ps.size())assert(a.add(p.data(),p.size(),10+i)==Result::Duplicate);
    }
    assert(a.total==n && memcmp(a.body,b.data(),n)==0);a.accept();
    for(auto& p:ps)assert(a.add(p.data(),p.size(),100)==Result::Duplicate);
  }
  std::vector<uint8_t> b(500,0x55);auto ps=encode(b,1,10);Assembler a;
  assert(a.add(ps[0].data(),ps[0].size(),0)==Result::Pending);
  auto other=encode(b,1,11);assert(a.add(other[0].data(),other[0].size(),1)==Result::Busy);
  assert(a.add(other[0].data(),other[0].size(),120001)==Result::Pending);
  auto corrupt=other[1];corrupt[16]^=1;assert(a.add(corrupt.data(),corrupt.size(),120002)==Result::Pending);
  assert(a.add(other[2].data(),other[2].size(),120003)==Result::Invalid);
  assert(a.add(ps[0].data(),16,0)==Result::Invalid);
  auto bad=ps[0];put16(bad.data()+10,1);assert(a.add(bad.data(),bad.size(),0)==Result::Invalid);
  bad=ps[0];bad.pop_back();assert(a.add(bad.data(),bad.size(),0)==Result::Invalid);
  // Random malformed input must not overflow the decoder.
  uint32_t r=12345;uint8_t fuzz[300];
  for(int n=0;n<100000;n++){for(auto& c:fuzz){r=r*1664525+1013904223;c=r>>24;}a.add(fuzz,n%301,n);}
  puts("Protocol tests passed: CRC, reordering, duplicates, timeout, corruption, bounds, 100000 malformed packets.");
}
