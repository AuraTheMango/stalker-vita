
#pragma once
#include "vita_assault_path.h"
#include <algorithm>
#include <limits>
#include <queue>

// Bounded, load-time search from the approach marker to a collision-safe firing position.
// The original patrol ends outside the farm; that marker alone cannot start the encounter.
struct VitaAssaultAdvanceStats { unsigned visited=0; bool ready=false; };

template<class Ground>
bool find_vita_assault_advance(Ground& ground,const VitaPatrolPoint& start,
 const VitaPatrolPoint (&targets)[6],const std::array<bool,6>& present,float range,
 std::vector<VitaPatrolPoint>& path,VitaAssaultAdvanceStats& stats)
{
 stats={};
 if(std::none_of(present.begin(),present.end(),[](bool value){return value;}))return false;
 constexpr int radius=40,width=radius*2+1;
 constexpr unsigned visit_limit=2200;
 struct Node { float cost=std::numeric_limits<float>::infinity(),y=0; int parent=-1; bool closed=false; };
 struct Open {float score;int id;bool operator<(const Open& other)const{return score>other.score;}};
 std::vector<Node> nodes(width*width);
 std::priority_queue<Open> open;
 const auto point=[&](int id,float y){return VitaPatrolPoint{start[0]+id%width-radius,y,start[2]+id/width-radius};};
 const auto estimate=[&](const VitaPatrolPoint& p){
  float best=1000.f;
  for(unsigned j=0;j<6;++j)if(present[j])best=std::min(best,std::max(0.f,std::hypot(p[0]-targets[j][0],p[2]-targets[j][2])-range));
  return best;
 };
 const auto firing_position=[&](const VitaPatrolPoint& p){
  for(unsigned j=0;j<6;++j)if(present[j]){
   const float dx=targets[j][0]-p[0],dy=targets[j][1]+1.2f-p[1]-1.45f,dz=targets[j][2]-p[2];
   const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
   if(distance<1.f||distance>range)continue;
   const float origin[]={p[0],p[1]+1.45f,p[2]},direction[]={dx/distance,dy/distance,dz/distance};
   if(!ground.blocks_ray(origin,direction,distance-.2f))return true;
  }
  return false;
 };
 stats={};
 const int first=radius*width+radius;
 nodes[first].cost=0;nodes[first].y=start[1];open.push({estimate(start),first});
 int finish=-1;
 while(!open.empty()&&stats.visited<visit_limit)
 {
  const int current=open.top().id;open.pop();
  auto& node=nodes[current];if(node.closed)continue;
  node.closed=true;++stats.visited;
  const auto from=point(current,node.y);
  if(firing_position(from)){finish=current;break;}
  const int x=current%width,z=current/width;
  for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)
  {
   if((!dx&&!dz)||x+dx<0||x+dx>=width||z+dz<0||z+dz>=width)continue;
   const int next=(z+dz)*width+x+dx;
   auto& candidate=nodes[next];
   const float cost=node.cost+(dx&&dz?1.41421356f:1.f);
   if(candidate.closed||cost>=candidate.cost)continue;
   auto to=point(next,node.y);
   ground.slide_move(from[0],from[2],node.y,to[0],to[2]);
   const auto requested=point(next,node.y);
   if(std::hypot(to[0]-requested[0],to[2]-requested[2])>.05f||
    !ground.height_at(to[0],to[2],node.y,.45f,.65f,to[1]))continue;
   candidate.cost=cost;candidate.y=to[1];candidate.parent=current;
   open.push({cost+estimate(to),next});
  }
 }
 if(finish<0)return false;
 std::vector<VitaPatrolPoint> result;
 for(int id=finish;id!=first;id=nodes[id].parent)
 {
  if(id<0||result.size()>nodes.size())return false;
  result.push_back(point(id,nodes[id].y));
 }
 std::reverse(result.begin(),result.end());
 path.swap(result);stats.ready=true;return true;
}
