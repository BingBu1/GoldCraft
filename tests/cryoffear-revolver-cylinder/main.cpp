// SPDX-License-Identifier: GPL-3.0-or-later
#include <metahook.h>
#include "native/guest/cof/client/revolver_cylinder.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
static_assert(offsetof(cl_entity_t,curstate)+offsetof(entity_state_t,controller)==0x318);
static_assert(offsetof(cl_entity_t,latched)+offsetof(latchedvars_t,prevcontroller)==0xb3c);
int main(int argc,char** argv)
{
 if(argc!=3)return 1;std::ifstream cases(argv[1]),expected(argv[2]);if(!cases||!expected)return 1;
 std::string line,wanted;unsigned rows{};
 while(std::getline(cases,line))
 {
  std::array<unsigned,5> row{};std::array<unsigned,3> want{},got{};std::istringstream values(line);
  for(auto& value:row)if(!(values>>std::hex>>value))return 3;
  if(values>>std::ws&&values.peek()!=std::char_traits<char>::eof())return 3;
  if(!std::getline(expected,wanted))return 4;std::istringstream results(wanted);for(auto& value:want)if(!(results>>std::hex>>value))return 4;
  if(results>>std::ws&&results.peek()!=std::char_traits<char>::eof())return 4;
  cl_entity_t entity{},other{};std::memcpy(entity.curstate.controller,&row[3],4);std::memcpy(entity.latched.prevcontroller,&row[4],4);
  const auto* view=row[1]==1?&entity:row[1]==2?&other:nullptr;
  got[0]=guest::cof::client::ApplyRevolverCylinder(entity,view,row[2],std::bit_cast<float>(row[0]));
  std::memcpy(&got[1],entity.curstate.controller,4);std::memcpy(&got[2],entity.latched.prevcontroller,4);
  if(got!=want){std::cerr<<"Revolver original controller mismatch "<<rows<<'\n';return 5;}++rows;
 }
 if(!rows)return 6;if(std::getline(expected,wanted))return 7;
 std::cout<<rows<<" original Revolver cylinder/controller cases; zero differences; fourth controller retained\n";
}
