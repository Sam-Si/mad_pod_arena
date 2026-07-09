
#include "src/engine/engine.h"
#include "src/cg/ga_pure.h"
#include "src/core/progress.h"
#include "src/core/maps/catalog.h"
#include <cmath>
#include <iostream>
static int g_fails=0;
#define CHECK(cond) do{if(!(cond)){std::cerr<<"FAIL "<<#cond<<"\n";++g_fails;}}while(0)
int main(){
  InitLUT(); SeedRand(1); (void)FastRand(); (void)FastRandInt(1,3);
  Timer t; t.Start(); (void)t.ElapsedMs();
  Vec2 a(0,0), b(3,4); CHECK(a.Distance(b)==5.0);
  CHECK(std::fabs(GameEngine::NormalizeAngle(400)-40)<1e-9);
  CHECK(std::fabs(GameEngine::ShortestAngleDiff(10,350)+20)<1e-9);
  CHECK(Round(2.6)==3.0);
  Pod p; p.angle=-1; p.ApplyGAAction(0,200); p.ApplyServerAction(10,0,100);
  p.shield_cd=4; (void)p.Mass(); p.EndTurn();
  Pod pods[4];
  for(int i=0;i<4;i++){ pods[i]=Pod(); pods[i].id=i; pods[i].pos={1000.+i*20,1000}; pods[i].vel={50,0}; pods[i].angle=0;}
  FastSimulateTurn(pods);
  // ga_pure already covered elsewhere but touch via engine build too
  CHECK(ga_pure::ClampAngleShiftDeg(30)==18);
  CHECK(csb_progress::GlobalNext(0,1,4)==1);
  CHECK(GetTournamentMapCount()==18);
  if(g_fails) return 1;
  std::cout<<"engine_branch_suite ok\n"; return 0;
}
