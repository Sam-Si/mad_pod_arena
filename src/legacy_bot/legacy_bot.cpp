//============================================================================
// Name        : Coders Strike Back
// Author      : me
// Version     : Rambot & Runbot (Fixed)
// Copyright   :
// Description : Fixed version - replaced broken neural bots with heuristics
//============================================================================

#include <iostream>
#include <sstream>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <utility>

#ifdef COMPILE_FOR_CALLGRIND
#include <valgrind/callgrind.h>
#endif

#define LOG(X)  //std::cerr << X << std::endl
#define LOGA(X) //LOG(X)
#define LOGB(X) //LOG(X)
#define LOGRECUIT(X) //LOG(X)
#define LOGC(X) //LOG(X)
#define LOGD(X) //LOG(X)
#define LOGE(X) //LOG(X)
#define LOGMETABOT(X) //LOG(X)
#define LOGINPUT(X) //LOG(X)

//#define LOCAL_SERVER
//#define STEPNUMBERBASED
constexpr int STEPBASEDMAXSTEP = 15000;

/*******************************************************************************
*
*     DEFINE GAME CONSTANTS
*
*******************************************************************************/
constexpr double PI                    = 3.14159265359;
constexpr double EPSILON               = 1e-8;
constexpr double BIGDOUBLE             = 1e8;

constexpr int    NPLAYERS              = 2;
constexpr int    NSHIPS                = 2;
constexpr int    NBEACONSMAX           = 8;
constexpr double MAXDELTAANGLE         = 18.0*PI/180.0;
constexpr int    MINTHRUST             = 0;
constexpr int    MAXTHRUST             = 200;
constexpr int    NLAPSMAX              = 20;
constexpr double SHIPRADIUS            = 400.0;
constexpr double REALBEACONRADIUS      = 600.0;
constexpr double SMALLBEACONRADIUS     = 565.0;

//Recuit génétique
constexpr int RG_MAXPOOLSIZE           = 48;
constexpr int RG_MAXMOVESEQUENCESIZE   = 6;
constexpr int RG_TOURNAMENTPOPULATION  = 2;
constexpr int RG_STEPSIZE              = 1000;
constexpr int RG_TIMERSTEPSIZE         = 20000;

//Mutations de MoveSequenceBot
constexpr int  MS_MAXUSABLESHIELDSTEP       = 3;
constexpr int  MS_MAXRANDSHIELDSTEP         = 8;
constexpr double MS_MAXRANDOMDELTAANGLE     = 40.0*PI/180.0;
constexpr int MS_MAXRANDOMTHRUST            = 500;
constexpr int MS_MINRANDOMTHRUST            = -100;
constexpr double MS_SMALLMUTATIONDELTAANGLE = 12.0*PI/180.0;
constexpr int MS_SMALLMUTATIONTHRUSTVALUE   = 50;

//Various values
constexpr int OPPONENTTIMEBUDGET             = 10000;
constexpr int MYTIMEBUDGET                   = 75000;
constexpr int SIMULATION_HOWLONGUSERUNNINGNN = 0;
constexpr int SIMULATION_HOWLONGUSERAMMINGNN = 0;
constexpr int POOLSIZE                       = RG_MAXPOOLSIZE;
constexpr int SEQUENCESIZE                   = RG_MAXMOVESEQUENCESIZE;

constexpr double DECALAGEBEACONENTRYPOINT           = 300.0;
constexpr double OPPONENTRAMBOTTAKESPEEDINTOACCOUNT = 1.0;
constexpr double OPPONENTRAMBOTAIMINFRONT           = 500.0;
constexpr bool   COOPERATIVERAMBEACON               = false;
constexpr double RAMRESTPOINTRADIUS                 = COOPERATIVERAMBEACON ? 500 : -1000.0;
constexpr double ACCEPTABLEDISTANCETORAMRESTPOINT   = 300.0;
constexpr double COEFFGETAWAYFROMRAM                = 3.0;
constexpr int    MAXTURNSGETAWAYFROMRAM             = 2;

constexpr double FRICTION                           = 0.85;
constexpr double COEFFPRIORITEHIGHTHRUST            = 0.13;
constexpr double SHIELDCOST                         = 330;

constexpr double COEFFEVAL_GAMEWON                  = 1000.0;
constexpr double COEFFEVAL_GLOBALRAM                = 1.5;
constexpr double COEFFEVAL_RAM_MAINTAINEYESONTARGET = 20.0;
constexpr double COEFFEVAL_RAM_STAYINFRONTOFHIM     = COOPERATIVERAMBEACON ? 0.0 : 20.0;
constexpr double COEFFEVAL_RAM_REPLACENEAROBJECTIVE = 0.03;
constexpr double COEFFEVAL_RAM_BEACONACTIVATIONTIME = 1000.0;
constexpr double COEFFEVAL_GLOBALTURNZERO           = 0.05;
constexpr double COEFFEVAL_ILIKETOGOFAST            = 0.08;
constexpr double COEFFEVAL_BYPASSOPPONENTRAMBOT     = 20.0;
constexpr double COEFFEVAL_ACTIVATEITFAST           = 30.0;

/*******************************************************************************
*
*     TIME HELPER FUNCTIONS
*
*******************************************************************************/

typedef std::chrono::high_resolution_clock::time_point time_point;
thread_local time_point _startTime;
inline time_point getCurrentTime() {
  return std::chrono::high_resolution_clock::now();
}
inline void resetTimer() {
  _startTime = getCurrentTime();
}
inline int getTimerDuration() {
  return std::chrono::duration_cast<std::chrono::microseconds>(getCurrentTime() - _startTime).count();
}

/*******************************************************************************
*
*     RANDOM HELPER FUNCTIONS
*
*******************************************************************************/
thread_local unsigned int g_seed;
inline void fast_srand(int seed) {
  g_seed = seed;
}
inline int fastrand() {
  g_seed = (214013*g_seed+2531011);
  return (g_seed>>16)&0x7FFF;
}
inline int fastRandInt(int maxSize) {
  return fastrand() % maxSize;
}
inline int fastRandInt(int a, int b) {
  return(a + fastRandInt(b - a));
}
inline double fastRandDouble() {
  return static_cast<double>(fastrand()) / 0x7FFF;
}
inline double fastRandDouble(double a, double b) {
  return a + (static_cast<double>(fastrand()) / 0x7FFF)*(b-a);
}
/******************************************************************************
*
*     ANGLE HELPER TYPE
*
*******************************************************************************/
class Angle {
public :
  double angleValue;
  Angle() {
    angleValue = 0.0;
  }
  Angle(double angle) {
    if(angle >= 0.0)
      angleValue = angle;
    else
      angleValue = angle + 2.0*PI;
  }
  Angle& operator+=(const Angle& angle) {
    angleValue += angle.angleValue;
    if(angleValue >= PI) angleValue -= 2.0*PI;
    else if(angleValue < -PI) angleValue += 2.0*PI;
    return *this;
  }
  Angle& operator-=(const Angle& angle) {
    angleValue -= angle.angleValue;
    if(angleValue >= PI) angleValue -= 2.0*PI;
    else if(angleValue < -PI) angleValue += 2.0*PI;
    return *this;
  }
};
Angle operator+(const Angle p1, const Angle p2) {
  return Angle(p1) += p2;
}
Angle operator-(const Angle p1, const Angle p2) {
  return Angle(p1) -= p2;
}
inline Angle mean(Angle a1, Angle a2) {
  if(abs(a1.angleValue-a2.angleValue)>PI)
    return Angle((a1.angleValue+a2.angleValue)*0.5)+Angle(PI);
  else
    return Angle((a1.angleValue+a2.angleValue)*0.5);
}

/******************************************************************************
*
*     VEC2 HELPER TYPE
*
*******************************************************************************/
class vec2 {
public:
  double x,y;
  vec2() {};
  vec2(double xx, double yy) {
    x=xx;
    y=yy;
  }
  vec2& operator+=(const vec2& rhs) {
    x += rhs.x;
    y += rhs.y;
    return *this;
  }
  vec2& operator-=(const vec2& rhs) {
    x -= rhs.x;
    y -= rhs.y;
    return *this;
  }
  vec2 operator-() const {
    return vec2(-x, -y);
  }
  template<typename constant_type>
  vec2& operator*=(const constant_type constant) {
    x *= constant;
    y *= constant;
    return *this;
  }
  template<typename constant_type>
  vec2& operator/=(const constant_type constant) {
    x /= constant;
    y /= constant;
    return *this;
  }
};
inline vec2 operator-(const vec2 &a, const vec2 &b) {
  return {a.x-b.x, a.y-b.y};
}
inline vec2 operator+(const vec2 &a, const vec2 &b) {
  return {a.x+b.x,a.y+b.y};
}
inline vec2 operator/(const vec2 &a, double b) {
  return {double(a.x/b),double(a.y/b)};
}
inline vec2 operator*(double b,const vec2 &a) {
  return {double(a.x*b),double(a.y*b)};
}
inline vec2 operator*(const vec2 &a, double b) {
  return {double(a.x*b),double(a.y*b)};
}
inline double dot(const vec2 &a, const vec2 &b) {
  return a.x*b.x+a.y*b.y;
}
inline double cross(const vec2 &vec, const vec2 &axe) {
  return vec.y*axe.x-vec.x*axe.y;
}
inline double norm(vec2 a) {
  return std::sqrt(a.x*a.x+a.y*a.y);
}
inline double normSq(vec2 a) {
  return a.x*a.x + a.y*a.y;
}
inline vec2 vec2ByAngle(double angle) {
  return (vec2) {
    cos(angle), sin(angle)
  };
}
inline vec2 vec2ByAngle(Angle angle) {
  return (vec2) {
    cos(angle.angleValue), sin(angle.angleValue)
  };
}
inline double angleByvec2(vec2 vec) {
  vec = vec/norm(vec);
  double angle = acos(vec.x);
  return (vec.y < 0.0) ? 2.0*PI-angle : angle;
}
inline vec2 rotateQuarter(vec2 vec) {
  return vec2(-vec.y, vec.x);
}

std::ostream& operator<<(std::ostream& out, const vec2& a) {
  return out<< a.x <<" "<<a.y;
}

/******************************************************************************
*
*     RANDOM STUFF
*
*******************************************************************************/
inline double limitDeltaAngle(double deltaAngle) {
  return (deltaAngle > MAXDELTAANGLE) ? MAXDELTAANGLE : ((deltaAngle < -MAXDELTAANGLE) ? -MAXDELTAANGLE : deltaAngle);
}
inline int limitThrust(int thrust) {
  return (thrust > MAXTHRUST) ? MAXTHRUST : ((thrust < MINTHRUST) ? MINTHRUST : thrust);
}
inline int sgn(double x) {
  return x < 0.0 ? -1 : 1;
}
enum class Behavior {
  Default, Fastest, Svankoot
};


/******************************************************************************
*
*     CLASS GAMEACTION
*
*******************************************************************************/
class GameAction {
public:
  double deltaAngle;
  int thrust;
  bool isShield = false;

  GameAction() {};
  GameAction (double dAngle, int thr) {
    deltaAngle = limitDeltaAngle(dAngle);
    thrust = limitThrust(thr);
  }
  GameAction (double dAngle, int thr, bool shield) {
    deltaAngle = limitDeltaAngle(dAngle);
    thrust = limitThrust(thr);
    isShield = shield;
  }
};


/******************************************************************************
*
*     CLASS MOBILE et CLASS BEACON
*
*******************************************************************************/

class Mobile {
public :
  vec2 pos;
  vec2 speed;
  double tLastUpdate;

  virtual ~Mobile() {};

  inline void updatePosition(double newTime) {
    pos += speed * (newTime-tLastUpdate);
    tLastUpdate = newTime;
  }

  double intersectTime(Mobile &mobile2, double dsquared) {
    updatePosition(mobile2.tLastUpdate);

    double a = normSq(speed - mobile2.speed);
    double b = 2.0*dot(pos-mobile2.pos, speed-mobile2.speed);
    double c = normSq(pos-mobile2.pos) - dsquared;

    double delta = b*b-4.0*a*c;

    if (delta < 0) {
      return BIGDOUBLE;
    } else if(a < EPSILON) {
      return BIGDOUBLE;
    } else {
      return tLastUpdate +(-b - std::sqrt(delta))/(2.0*a);
    }
  }
};

class Beacon : public Mobile {
public:
  vec2 entryPoint;
  vec2 exitPoint;
  vec2 midPoint;
  vec2 ramRestPoint;
  vec2 entryPointv2;
  double distToEnd[NLAPSMAX];
};


/******************************************************************************
*
*     CLASS SHIP and CLASS PLAYER
*
*******************************************************************************/
class Ship : public Mobile {
public:
  int lapNumber = 0;
  int nextBeacon = 1;
  double tGameEnd = BIGDOUBLE;
  Angle angle;
  double beaconActivationTime = (double)SEQUENCESIZE+0.3;
  int shieldCounter = 0;
  double inverseShipMass = 1.0;


  inline void truncateAll() {
    pos.x = round(pos.x);
    pos.y = round(pos.y);
    speed.x = trunc(speed.x);
    speed.y = trunc(speed.y);
  }

  inline void applyGameAction(GameAction &gameAction) {
    if(gameAction.isShield) {
      shieldCounter = 4;
      inverseShipMass = 0.1;
    }
    angle += Angle(gameAction.deltaAngle);
    if(shieldCounter <= 0) {
      speed += vec2ByAngle(angle)*gameAction.thrust;
    }
    if(shieldCounter == 3)
      inverseShipMass = 1.0;
    --shieldCounter;
  }
};


class Player {
public:
  int timeout = 0;
};




/******************************************************************************
*
*     CLASS GAME & CLASS MAPDATA
*
*******************************************************************************/
class MapData {
public:
  Beacon beacons[NBEACONSMAX];
  int nBeacons;
  int nLaps;
  Player players[2];
  int bestShipLapNumber=0;
  int bestShipCheckpointNumber=0;

  void preCalculateBeaconStuff() {
    for(int i = 0; i < nBeacons; ++i) {
      vec2 vecB1B2 = beacons[(i+1)%nBeacons].pos - beacons[i].pos;
      vec2 pointCible = beacons[i].pos + SMALLBEACONRADIUS*vecB1B2/norm(vecB1B2);
      beacons[i].exitPoint = pointCible;

      vecB1B2 = beacons[(i+nBeacons-1)%nBeacons].pos - beacons[i].pos;
      pointCible = beacons[i].pos + SMALLBEACONRADIUS*vecB1B2/norm(vecB1B2);
      beacons[i].entryPoint = pointCible;

      beacons[i].midPoint = (beacons[i].entryPoint + beacons[i].exitPoint)*0.5;

      vecB1B2 = beacons[(i+nBeacons-1)%nBeacons].pos - beacons[(i+1)%nBeacons].pos;
      pointCible = beacons[i].pos + DECALAGEBEACONENTRYPOINT*vecB1B2/norm(vecB1B2);
      beacons[i].entryPointv2 = pointCible;

      vecB1B2 = beacons[(i+nBeacons-1)%nBeacons].pos + beacons[(i+1)%nBeacons].pos - 2.0 * beacons[i].pos;
      pointCible = beacons[i].pos - RAMRESTPOINTRADIUS*vecB1B2/norm(vecB1B2);
      beacons[i].ramRestPoint = pointCible;
    }

    double distToEnd = 0;
    for(int lap = nLaps; lap >= 0; --lap) {
      for(int cp = nBeacons; cp >=1 ; --cp) {
        Beacon& b1 = beacons[cp%nBeacons];
        Beacon& b2 = beacons[(cp+1)%nBeacons];
        distToEnd += norm(b2.entryPoint - b1.exitPoint);
        distToEnd += norm(b1.exitPoint - b1.entryPoint);
        distToEnd += 3.3 * SMALLBEACONRADIUS;

        b1.distToEnd[lap] = distToEnd;
      }
    }
  }
};

class Game {
public:
  Ship ships[NPLAYERS*NSHIPS];
  int turnNumber = 0;
  double gameEndTime = BIGDOUBLE;
  int gameWinner = 0;
  MapData *mapData;

  double evaluateSinglePlayerSimulation(int shipId, Behavior behavior) {
    double evaluation = 0.0;
    if(behavior != Behavior::Svankoot) {
      if(gameEndTime >= BIGDOUBLE - 1.0) {
        evaluation -= (norm(ships[shipId].pos-mapData->beacons[ships[shipId].nextBeacon].entryPointv2));
        evaluation -= mapData->beacons[ships[shipId].nextBeacon].distToEnd[ships[shipId].lapNumber];

        if(behavior == Behavior::Default) {
        }
        return evaluation;
      } else {
        if(ships[shipId].tGameEnd >= BIGDOUBLE - 1.0) {
          evaluation -= BIGDOUBLE;
          evaluation -= (norm(ships[shipId].pos-mapData->beacons[ships[shipId].nextBeacon].pos)-SMALLBEACONRADIUS);
          evaluation -= mapData->beacons[ships[shipId].nextBeacon].distToEnd[ships[shipId].lapNumber];
          return evaluation;
        } else if (gameWinner != shipId) {
          evaluation -= BIGDOUBLE;
          evaluation -= 1000*ships[shipId].tGameEnd;
          return evaluation;
        } else if (gameWinner == shipId) {
          evaluation += BIGDOUBLE;
          evaluation -= 1000*ships[shipId].tGameEnd;
          return evaluation;
        }
      }
    }
    return 1.0;
  }

  double evaluateCombinedSimulationTurnN(int myRunId, int myRamId, int oppRunId, int myRamBeacon, bool riskTimeout) {
    double evaluation = 0.0;
    if(gameEndTime >= BIGDOUBLE - 1.0) {
      evaluation -= (norm(ships[myRunId].pos-mapData->beacons[ships[myRunId].nextBeacon].entryPointv2));
      evaluation -= mapData->beacons[ships[myRunId].nextBeacon].distToEnd[ships[myRunId].lapNumber];

      vec2 tempVec = ships[oppRunId==2?3:2].pos-ships[myRunId].pos;
      vec2 tempVec2 = mapData->beacons[ships[myRunId].nextBeacon].pos - ships[myRunId].pos;
      evaluation += COEFFEVAL_BYPASSOPPONENTRAMBOT*atan2(abs(cross(tempVec, tempVec2)), dot(tempVec, tempVec2));

      evaluation -= COEFFEVAL_ACTIVATEITFAST*ships[myRunId].beaconActivationTime;

    } else {
      if(ships[myRunId].tGameEnd >= BIGDOUBLE - 1.0) {
        evaluation -= COEFFEVAL_GAMEWON;
        evaluation -= (norm(ships[myRunId].pos-mapData->beacons[ships[myRunId].nextBeacon].entryPointv2));
        evaluation -= mapData->beacons[ships[myRunId].nextBeacon].distToEnd[ships[myRunId].lapNumber];
      } else if (gameWinner != myRunId) {
        evaluation -= COEFFEVAL_GAMEWON;
        evaluation -= 1000*ships[myRunId].tGameEnd;
      } else if (gameWinner == myRunId) {
        evaluation += COEFFEVAL_GAMEWON;
        evaluation -= 1000*ships[myRunId].tGameEnd;
      }
    }

    if(!riskTimeout) {
      double d = norm(ships[myRamId].pos-mapData->beacons[myRamBeacon].ramRestPoint);
      evaluation -= COEFFEVAL_GLOBALRAM*COEFFEVAL_RAM_REPLACENEAROBJECTIVE*(d < ACCEPTABLEDISTANCETORAMRESTPOINT ? (d-ACCEPTABLEDISTANCETORAMRESTPOINT) * 0.1 : d- ACCEPTABLEDISTANCETORAMRESTPOINT);
      evaluation += COEFFEVAL_GLOBALRAM*COEFFEVAL_RAM_BEACONACTIVATIONTIME*ships[oppRunId].beaconActivationTime;

      vec2 tempVec = ships[myRamId].pos-ships[oppRunId].pos;
      vec2 tempVec2 =  mapData->beacons[myRamBeacon].pos-ships[oppRunId].pos;
      evaluation -= COEFFEVAL_GLOBALRAM*COEFFEVAL_RAM_STAYINFRONTOFHIM*atan2(abs(cross(tempVec, tempVec2)), dot(tempVec, tempVec2));
      evaluation -= COEFFEVAL_GLOBALRAM*COEFFEVAL_RAM_MAINTAINEYESONTARGET*abs(atan2(-tempVec.y, -tempVec.x)-ships[myRamId].angle.angleValue);

      if(ships[oppRunId].beaconActivationTime > (double)SEQUENCESIZE) {
        evaluation += COEFFEVAL_GLOBALRAM*(norm(ships[oppRunId].pos-mapData->beacons[ships[oppRunId].nextBeacon].pos));
      }
    } else {
      evaluation -= (norm(ships[myRamId].pos-mapData->beacons[ships[myRamId].nextBeacon].entryPointv2));
      evaluation -= mapData->beacons[ships[myRamId].nextBeacon].distToEnd[ships[myRamId].lapNumber];
    }
    return evaluation;
  }

  double evaluateCombinedSimulationTurnZero(int myRamId, int oppRunId, int myRamBeacon, bool riskTimeout) {
    double evaluation = 0.0;

    vec2 tempVec = ships[oppRunId==2?3:2].pos-ships[myRamId==1?0:1].pos;
    vec2 tempVec2 = mapData->beacons[ships[myRamId==1?0:1].nextBeacon].pos - ships[myRamId==1?0:1].pos;
    evaluation += COEFFEVAL_BYPASSOPPONENTRAMBOT+atan2(abs(cross(tempVec, tempVec2)), dot(tempVec, tempVec2));

    if(!riskTimeout) {
      vec2 tempVec = ships[myRamId].pos-ships[oppRunId].pos;
      vec2 tempVec2 =  mapData->beacons[myRamBeacon].pos-ships[oppRunId].pos;
      evaluation -= COEFFEVAL_GLOBALRAM*COEFFEVAL_RAM_STAYINFRONTOFHIM*atan2(abs(cross(tempVec, tempVec2)), dot(tempVec, tempVec2));
      evaluation -= COEFFEVAL_GLOBALRAM*COEFFEVAL_RAM_MAINTAINEYESONTARGET*abs(atan2(-tempVec.y, -tempVec.x)-ships[myRamId].angle.angleValue);

      double d = norm(ships[myRamId].pos-mapData->beacons[myRamBeacon].ramRestPoint);
      evaluation -= COEFFEVAL_GLOBALRAM*COEFFEVAL_RAM_REPLACENEAROBJECTIVE*(d < ACCEPTABLEDISTANCETORAMRESTPOINT ? (d-ACCEPTABLEDISTANCETORAMRESTPOINT) * 0.1 : d- ACCEPTABLEDISTANCETORAMRESTPOINT);
      evaluation += COEFFEVAL_GLOBALRAM*COEFFEVAL_RAM_BEACONACTIVATIONTIME*ships[oppRunId].beaconActivationTime;
    } else {
      evaluation -= (norm(ships[myRamId].pos-mapData->beacons[ships[myRamId].nextBeacon].entryPointv2));
      evaluation -= mapData->beacons[ships[myRamId].nextBeacon].distToEnd[ships[myRamId].lapNumber];
    }
    return COEFFEVAL_GLOBALTURNZERO*evaluation;
  }
};


/******************************************************************************
*
*     CLASS BOT
*
*******************************************************************************/
class Bot {
public :
  virtual ~Bot() {};
  virtual GameAction getNextAction(Game &game, int shipId)=0;
};


/******************************************************************************
*
*     CLASS GAMESIMULATOR
*
*******************************************************************************/

class GameSimulator {
public:
  Bot* bots[NPLAYERS*NSHIPS];
  Bot* defaultRunBot;
  Bot* defaultRamBot;
  Game simulGame;
  bool didCollide[NPLAYERS*NSHIPS];
  bool defaultWhenCollide[NPLAYERS*NSHIPS];
  bool simulateAllShips;
  int singleShipToSimulate;
  Behavior behavior;
  double currentTime;
  int myRunId, myRamId, oppRunId, oppRamId;
  int hisRunningBotDidCollide = -1;
  double singlePlayerMinDistanceToRam = BIGDOUBLE;
  int singlePlayerRamId;

  void setPolicyA(int myRamIdd) {
    singlePlayerRamId = myRamIdd;
    simulateAllShips = false;
    behavior = Behavior::Default;
  }

  void initializeSimulation() {
    currentTime = 0.0;
    hisRunningBotDidCollide = -1;
    singlePlayerMinDistanceToRam = BIGDOUBLE;
  }

  void simulateFutureSpecialA(int nTurnsDeep) {
    GameAction gameAction;
    vec2 tempVec;
    bool oneTurnFinished;

    bool foundIntersect;
    int ship1, ship2;
    double timeIntersect;
    double timeResult;
    double tmin, tmax;
    vec2 ramAngle, ramSpeed;

    while(simulGame.turnNumber < nTurnsDeep) {

      playHisRunBot();
      playHisRamBot();

      gameAction = bots[0]->getNextAction(simulGame, 0);
      simulGame.ships[0].applyGameAction(gameAction);
      gameAction = bots[1]->getNextAction(simulGame, 1);
      simulGame.ships[1].applyGameAction(gameAction);

      ++simulGame.turnNumber;

      oneTurnFinished = false;
      tmin = currentTime;
      tmax = (double) simulGame.turnNumber;
      while(!oneTurnFinished) {
        oneTurnFinished = true;
        tmin = currentTime;
        tmax = (double) simulGame.turnNumber;
        foundIntersect = false;
        timeIntersect = BIGDOUBLE;

        for (int i = 0; i < 4 ; ++i) {
          for (int j = i+1; j < 4; ++j) {
            timeResult = simulGame.ships[i].intersectTime(simulGame.ships[j],4*SHIPRADIUS*SHIPRADIUS);
            if (timeResult < tmax && timeResult > tmin && timeResult < timeIntersect) {
              ship1 = i;
              ship2 = j;
              timeIntersect = timeResult;
              foundIntersect = true;
            }
          }
        }
        if(foundIntersect) {
          tmax = timeIntersect;
          if((ship1 == oppRunId || ship2 == oppRunId) && hisRunningBotDidCollide < 0) {
            hisRunningBotDidCollide = simulGame.turnNumber;
          }
          bounce(simulGame.ships[ship1] , simulGame.ships[ship2], timeIntersect);
          oneTurnFinished = false;
        }
        currentTime = tmax;
        checkBeaconActivationSpecialA(tmin, tmax);
      }

      for(int i = 0; i < 4 ; ++i) {
        simulGame.ships[i].updatePosition((double)simulGame.turnNumber);
        simulGame.ships[i].speed *= FRICTION;
        simulGame.ships[i].truncateAll();
      }
    }
  }

  void playHisRunBot() {
    Ship& oppRunShip = simulGame.ships[oppRunId];
    vec2 tempVec, runAngle, runSpeed;
    if(hisRunningBotDidCollide > 0) {
      tempVec = simulGame.mapData->beacons[oppRunShip.nextBeacon].pos-oppRunShip.pos;
      tempVec = tempVec / norm(tempVec);
      runAngle = vec2ByAngle(oppRunShip.angle);
      runSpeed = oppRunShip.speed / norm(oppRunShip.speed);

      double angleDotDir = dot(runAngle, tempVec);
      double angleCrossDir = cross(runAngle, tempVec);
      double speedCrossDir = cross(runSpeed, tempVec);

      if(angleDotDir >= 0.0) {
        oppRunShip.angle -= std::min(MAXDELTAANGLE, std::max(-MAXDELTAANGLE, angleCrossDir));
      } else {
        oppRunShip.angle -= MAXDELTAANGLE*sgn(angleCrossDir);
      }

      runAngle = vec2ByAngle(oppRunShip.angle);

      angleDotDir = dot(runAngle, tempVec);
      angleCrossDir = cross(runAngle, tempVec);

      if(angleDotDir > 0) {
        if(sgn(angleCrossDir) != sgn(speedCrossDir)) {
          oppRunShip.speed += MAXTHRUST * runAngle;
        } else {
          oppRunShip.speed += MAXTHRUST * angleDotDir * tempVec;
          tempVec = rotateQuarter(tempVec);
          oppRunShip.speed += MAXTHRUST /2 * angleCrossDir * tempVec;
        }
      }
    } else {
      GameAction gameAction = bots[oppRunId]->getNextAction(simulGame, oppRunId);
      oppRunShip.applyGameAction(gameAction);
    }
  }

  void playHisRamBot() {
    Ship& oppRamShip = simulGame.ships[oppRamId];
    Ship& myRunShip = simulGame.ships[myRunId];

    vec2 tempVec, ramAngle, ramSpeed, dirRamBotMyNextBeacon;
    tempVec = myRunShip.pos + OPPONENTRAMBOTTAKESPEEDINTOACCOUNT * myRunShip.speed - simulGame.mapData->beacons[simulGame.ships[myRunId].nextBeacon].pos;
    tempVec /= norm(tempVec);
    tempVec = myRunShip.pos + OPPONENTRAMBOTTAKESPEEDINTOACCOUNT * myRunShip.speed - OPPONENTRAMBOTAIMINFRONT*tempVec-oppRamShip.pos;
    tempVec = tempVec / norm(tempVec);
    ramAngle = vec2ByAngle(oppRamShip.angle);
    ramSpeed = oppRamShip.speed / norm(oppRamShip.speed);
    dirRamBotMyNextBeacon = simulGame.mapData->beacons[simulGame.ships[myRunId].nextBeacon].pos-oppRamShip.pos;
    dirRamBotMyNextBeacon /= norm(dirRamBotMyNextBeacon);
    double coeffThrust = 1.0-std::max(dot(dirRamBotMyNextBeacon, tempVec)-0.5, 0.0)/0.5;
    double angleDotDir = dot(ramAngle, tempVec);
    double angleCrossDir = cross(ramAngle, tempVec);
    double speedCrossDir = cross(ramSpeed, tempVec);

    if(angleDotDir >= 0.0) {
      oppRamShip.angle -= std::min(MAXDELTAANGLE, std::max(-MAXDELTAANGLE, angleCrossDir));
    } else {
      oppRamShip.angle -= MAXDELTAANGLE*sgn(angleCrossDir);
    }

    ramAngle = vec2ByAngle(oppRamShip.angle);

    angleDotDir = dot(ramAngle, tempVec);
    angleCrossDir = cross(ramAngle, tempVec);

    if(angleDotDir > 0) {
      if(sgn(angleCrossDir) != sgn(speedCrossDir)) {
        oppRamShip.speed += MAXTHRUST * angleDotDir * tempVec * coeffThrust;
        tempVec = rotateQuarter(tempVec);
        oppRamShip.speed += MAXTHRUST * angleCrossDir * tempVec * coeffThrust;
      } else {
        oppRamShip.speed += MAXTHRUST * angleDotDir * tempVec * coeffThrust;
        tempVec = rotateQuarter(tempVec);
        oppRamShip.speed += MAXTHRUST /2 * angleCrossDir * tempVec * coeffThrust;
      }
    }
  }

  inline void checkBeaconActivationSpecialA(double tmin, double tmax) {
    double timeResult;
    Ship& myRunner = simulGame.ships[myRunId];
    timeResult = myRunner.intersectTime(simulGame.mapData->beacons[myRunner.nextBeacon],SMALLBEACONRADIUS*SMALLBEACONRADIUS);
    if(timeResult > tmin && timeResult < tmax) {
      myRunner.nextBeacon = (1+myRunner.nextBeacon)%simulGame.mapData->nBeacons;
      myRunner.beaconActivationTime = std::min(myRunner.beaconActivationTime, timeResult);
      if(myRunner.nextBeacon == 1) {
        myRunner.lapNumber += 1;
      }
      if(myRunner.lapNumber == simulGame.mapData->nLaps) {
        myRunner.tGameEnd = timeResult;
        if(timeResult < simulGame.gameEndTime) {
          simulGame.gameEndTime = timeResult;
          simulGame.gameWinner = myRunId;
        }
      }
    }

    Ship& hisRunner = simulGame.ships[oppRunId];
    timeResult = hisRunner.intersectTime(simulGame.mapData->beacons[hisRunner.nextBeacon], REALBEACONRADIUS * REALBEACONRADIUS);
    if(timeResult > tmin && timeResult < tmax) {
      hisRunner.beaconActivationTime = std::min(hisRunner.beaconActivationTime, timeResult);
      hisRunner.nextBeacon = (1+hisRunner.nextBeacon)%simulGame.mapData->nBeacons;
      if(hisRunner.nextBeacon == 1) {
        hisRunner.lapNumber += 1;
      }
      if(hisRunner.lapNumber == simulGame.mapData->nLaps) {
        hisRunner.tGameEnd = timeResult;
        if(timeResult < simulGame.gameEndTime) {
          simulGame.gameEndTime = timeResult;
          simulGame.gameWinner = oppRunId;
        }
      }
    }
  }


  void simulateFuture(int nTurnsDeep, int shipId) {
    if(simulateAllShips) {
    } else {
      singleShipToSimulate = shipId;
      simulateFutureOneShip(nTurnsDeep);
    }
  }

  void simulateFutureOneShip(int nTurnsDeep) {
    GameAction gameAction;
    Ship& ship = simulGame.ships[singleShipToSimulate];
    while(simulGame.turnNumber < nTurnsDeep) {
      if(simulGame.turnNumber < MAXTURNSGETAWAYFROMRAM) singlePlayerMinDistanceToRam = std::min(singlePlayerMinDistanceToRam, norm(ship.pos-simulGame.ships[singlePlayerRamId].pos));

      gameAction = bots[singleShipToSimulate]->getNextAction(simulGame, singleShipToSimulate);
      ship.applyGameAction(gameAction);

      ++simulGame.turnNumber;
      checkBeaconActivation(currentTime, (double)simulGame.turnNumber);

      ship.updatePosition((double)simulGame.turnNumber);
      ship.speed *= FRICTION;
      ship.truncateAll();
    }
  }

  inline void bounce(Ship &ship1, Ship &ship2, double time) {
    ship1.updatePosition(time);
    ship2.updatePosition(time);
    double combinedMass = 1.0/(ship1.inverseShipMass + ship2.inverseShipMass);
    vec2 dpos = ship1.pos-ship2.pos;
    vec2 dv = ship1.speed-ship2.speed;
    vec2 impulse = combinedMass * dpos * dot(dpos, dv) / normSq(dpos);
    ship1.speed -=  impulse * ship1.inverseShipMass;
    ship2.speed +=  impulse * ship2.inverseShipMass;
    impulse *= std::max(120.0/norm(impulse), 1.0);
    ship1.speed -=  impulse * ship1.inverseShipMass;
    ship2.speed +=  impulse * ship2.inverseShipMass;
  }

  inline void checkBeaconActivation(double tmin, double tmax) {
    double timeResult;
    Ship &ship = simulGame.ships[singleShipToSimulate];
    timeResult = ship.intersectTime(simulGame.mapData->beacons[ship.nextBeacon],SMALLBEACONRADIUS*SMALLBEACONRADIUS);
    if(timeResult > tmin && timeResult < tmax) {
      ship.nextBeacon = (1+ship.nextBeacon)%simulGame.mapData->nBeacons;
      if(ship.nextBeacon == 1) {
        ship.lapNumber += 1;
      }
      if(ship.lapNumber == simulGame.mapData->nLaps) {
        ship.tGameEnd = timeResult;
        if(timeResult < simulGame.gameEndTime) {
          simulGame.gameEndTime = timeResult;
          simulGame.gameWinner = singleShipToSimulate;
        }
      }
    }
  }
};


/******************************************************************************
*
*     VARIOUS GAME SPECIFIC BOTS
*
*******************************************************************************/

class MoveSequenceBot : public Bot {
public:
  int sequenceSize;
  double baseMutateChance;
  double deltaAngle[RG_MAXMOVESEQUENCESIZE];
  int thrustValue[RG_MAXMOVESEQUENCESIZE];
  int shieldStep;

  GameAction getNextAction(Game &game, int shipId) {
    if(shieldStep < MS_MAXUSABLESHIELDSTEP && game.turnNumber == shieldStep) {
      return GameAction(deltaAngle[game.turnNumber], thrustValue[game.turnNumber], true);
    } else {
      return GameAction(deltaAngle[game.turnNumber], thrustValue[game.turnNumber], false);
    }
  }
  void becomeRandomMutationFromOne(MoveSequenceBot &bot1, double amplitude) {
    double threshold = baseMutateChance + amplitude;
    for(int i = 0; i< sequenceSize ; ++i) {
      deltaAngle[i]  = (fastRandDouble() < threshold) ? fastRandDouble(-MS_MAXRANDOMDELTAANGLE, MS_MAXRANDOMDELTAANGLE) : bot1.deltaAngle[i];
      thrustValue[i] = (fastRandDouble() < threshold) ? fastRandInt(MS_MINRANDOMTHRUST, MS_MAXRANDOMTHRUST) : bot1.thrustValue[i];
    }
    if(fastRandDouble() < threshold) {
      shieldStep = fastRandInt(MS_MAXRANDSHIELDSTEP);
    }
  }
  void becomeRandomMutationFromTwo(MoveSequenceBot &bot1, MoveSequenceBot &bot2) {
    for(int i = 0; i < sequenceSize ; ++i) {
      deltaAngle[i] = (fastRandDouble() < 0.5) ? bot1.deltaAngle[i] : bot2.deltaAngle[i];
      thrustValue[i] = (fastRandDouble() < 0.5) ? bot1.thrustValue[i] : bot2.thrustValue[i];
    }
    shieldStep = (fastRandDouble() < 0.5) ? bot1.shieldStep : bot2.shieldStep;
  }
  void smallMutation() {
    deltaAngle[fastRandInt(sequenceSize)] += fastRandDouble(-MS_SMALLMUTATIONDELTAANGLE,MS_SMALLMUTATIONDELTAANGLE);
    thrustValue[fastRandInt(sequenceSize)] += fastRandInt(-MS_SMALLMUTATIONTHRUSTVALUE, MS_SMALLMUTATIONTHRUSTVALUE);
  }

  void dump() {
    std::stringstream ss;
    ss.precision(4);
    ss << "Dump MS  : ";
    for(int i = 0; i < sequenceSize; ++i) {
      ss << std::setw(6) << limitDeltaAngle(deltaAngle[i])*180.0/PI << " ";
    }
    ss.str(std::string());
    ss << "  Shield " << shieldStep << " : ";
    for(int i = 0; i < sequenceSize; ++i) {
      ss << std::setw(6) << limitThrust(thrustValue[i]) << " ";
    }
  }
};


// ============================================================================
// FIXED: Replaced broken NeuralRunBot with a working heuristic
// The original had uninitialized weight arrays producing garbage output.
// This heuristic aims at the next checkpoint with proper angle clamping.
// ============================================================================
class NeuralRunBot : public Bot {
public:
  GameAction getNextAction(Game &game, int playerId) {
    GameAction ga;
    vec2 dir = game.mapData->beacons[game.ships[playerId].nextBeacon].pos - game.ships[playerId].pos;
    double targetAngle = angleByvec2(dir);
    double angleDiff = targetAngle - game.ships[playerId].angle.angleValue;
    // Normalize to [-PI, PI]
    while(angleDiff > PI) angleDiff -= 2.0*PI;
    while(angleDiff < -PI) angleDiff += 2.0*PI;
    ga.deltaAngle = limitDeltaAngle(angleDiff);
    ga.thrust = MAXTHRUST;
    ga.isShield = false;
    return ga;
  }
};


// ============================================================================
// FIXED: Replaced broken NeuralRamBot with a working heuristic
// The original had uninitialized weight arrays AND never used targetShip.
// This heuristic aims at the target ship with velocity lead prediction.
// ============================================================================
class NeuralRamBot : public Bot {
  int targetShip = 0;

public:
  void setTarget(int shipId) {
    targetShip = shipId;
  }

  GameAction getNextAction(Game &game, int playerId) {
    GameAction ga;
    // Aim at target ship with 1-turn velocity lead
    vec2 target = game.ships[targetShip].pos + game.ships[targetShip].speed;
    vec2 dir = target - game.ships[playerId].pos;
    double d = norm(dir);
    if(d < EPSILON) {
      ga.deltaAngle = 0.0;
      ga.thrust = MAXTHRUST;
      ga.isShield = false;
      return ga;
    }
    double targetAngle = angleByvec2(dir);
    double angleDiff = targetAngle - game.ships[playerId].angle.angleValue;
    // Normalize to [-PI, PI]
    while(angleDiff > PI) angleDiff -= 2.0*PI;
    while(angleDiff < -PI) angleDiff += 2.0*PI;
    ga.deltaAngle = limitDeltaAngle(angleDiff);
    ga.thrust = MAXTHRUST;
    ga.isShield = false;
    return ga;
  }
};

/******************************************************************************
*
*     CLASS RECUITGENETIQUEBOT
*
*******************************************************************************/

class RecuitGenetiqueBot : public Bot {
public:
  MoveSequenceBot bots[2][RG_MAXPOOLSIZE];
  double botScores[RG_MAXPOOLSIZE];
  int poolSize;
  int sequenceSize;
  int weakestBotIndex = 0;
  int strongestBotIndex = 0;
  double bestWeakestBotScore = -BIGDOUBLE;
  int timeBudget;
  GameSimulator gameSimulator;
  time_point internalStartTime;
  bool useCombinedCase = false;
  int myRunId, myRamId, oppRunId, oppRamId, myRamBeacon;
  bool riskTimeout = false;

  void setupRecuitGenetiqueNotCombined(int timeBudgett, int poolSizee, int sequenceSizee) {
    useCombinedCase = false;
    timeBudget = timeBudgett;
    poolSize = poolSizee;
    sequenceSize = sequenceSizee;
    resetInternalTimer();
    for(int i = 0; i < sequenceSize ; ++i) {
      bots[0][i].sequenceSize = sequenceSize;
      bots[0][i].baseMutateChance = 1.5/sequenceSize;
      for(int j = 0; j<sequenceSize; ++j) {
        bots[0][i].deltaAngle[j] = MAXDELTAANGLE;
        bots[0][i].thrustValue[j] = (j>=i) ? MAXTHRUST : MINTHRUST;
      }
    }
    for(int i = 0; i < sequenceSize ; ++i) {
      bots[0][i+sequenceSize].sequenceSize = sequenceSize;
      bots[0][i+sequenceSize].baseMutateChance = 1.5/sequenceSize;
      for(int j = 0; j<sequenceSize; ++j) {
        bots[0][i+sequenceSize].deltaAngle[j] = -MAXDELTAANGLE;
        bots[0][i+sequenceSize].thrustValue[j] = (j>=i) ? MAXTHRUST : MINTHRUST;
      }
    }
    for(int i = 0; i < poolSize - 2*sequenceSize ; ++i) {
      bots[0][i+2*sequenceSize].sequenceSize = sequenceSize;
      bots[0][i+2*sequenceSize].baseMutateChance = 1.2/sequenceSize;
      for(int j = 0; j<sequenceSize; ++j) {
        bots[0][i+2*sequenceSize].deltaAngle[j] = -MAXDELTAANGLE + (i+1.0)/(poolSize-2.0*sequenceSize+1)*2.0*MAXDELTAANGLE;
        bots[0][i+2*sequenceSize].thrustValue[j] = fastRandInt(MINTHRUST, MAXTHRUST);
      }
    }
  }

  void setupRecuitGenetiqueCombined(int timeBudgett, int poolSizee, int sequenceSizee, int myRunIdd, int myRamIdd, int oppRunIdd, int oppRamIdd,int myRamBeacond, bool riskTimeoutt) {
    myRunId = myRunIdd;
    myRamId = myRamIdd;
    oppRunId = oppRunIdd;
    oppRamId = oppRamIdd;
    riskTimeout = riskTimeoutt;
    gameSimulator.myRunId = myRunIdd;
    gameSimulator.myRamId = myRamIdd;
    gameSimulator.oppRunId = oppRunIdd;
    gameSimulator.oppRamId = oppRamIdd;
    myRamBeacon = myRamBeacond;
    useCombinedCase = true;
    timeBudget = timeBudgett;
    poolSize = poolSizee;
    sequenceSize = sequenceSizee;

    resetInternalTimer();
    for(int k = 0; k< 2; ++k) {
      for(int i = 0; i < sequenceSize ; ++i) {
        bots[k][i].sequenceSize = sequenceSize;
        bots[k][i].baseMutateChance = 1.5/sequenceSize;
        bots[k][i].shieldStep = 0;
        for(int j = 0; j<sequenceSize; ++j) {
          bots[k][i].deltaAngle[j] = MAXDELTAANGLE;
          bots[k][i].thrustValue[j] = (j>=i) ? MAXTHRUST : MINTHRUST;
        }
      }
      for(int i = 0; i < sequenceSize ; ++i) {
        bots[k][i+sequenceSize].sequenceSize = sequenceSize;
        bots[k][i+sequenceSize].baseMutateChance = 1.5/sequenceSize;
        bots[k][i+sequenceSize].shieldStep = 0;
        for(int j = 0; j<sequenceSize; ++j) {
          bots[k][i+sequenceSize].deltaAngle[j] = -MAXDELTAANGLE;
          bots[k][i+sequenceSize].thrustValue[j] = (j>=i) ? MAXTHRUST : MINTHRUST;
        }
      }
      for(int i = 0; i < sequenceSize ; ++i) {
        bots[k][i+2*sequenceSize].sequenceSize = sequenceSize;
        bots[k][i+2*sequenceSize].baseMutateChance = 1.5/sequenceSize;
        bots[k][i+2*sequenceSize].shieldStep = MS_MAXRANDSHIELDSTEP;
        for(int j = 0; j<sequenceSize; ++j) {
          bots[k][i+2*sequenceSize].deltaAngle[j] = MAXDELTAANGLE;
          bots[k][i+2*sequenceSize].thrustValue[j] = (j>=i) ? MAXTHRUST : MINTHRUST;
        }
      }
      for(int i = 0; i < sequenceSize ; ++i) {
        bots[k][i+3*sequenceSize].sequenceSize = sequenceSize;
        bots[k][i+3*sequenceSize].baseMutateChance = 1.5/sequenceSize;
        bots[k][i+3*sequenceSize].shieldStep = MS_MAXRANDSHIELDSTEP;
        for(int j = 0; j<sequenceSize; ++j) {
          bots[k][i+3*sequenceSize].deltaAngle[j] = -MAXDELTAANGLE;
          bots[k][i+3*sequenceSize].thrustValue[j] = (j>=i) ? MAXTHRUST : MINTHRUST;
        }
      }
      for(int i = 0; i < poolSize - 4*sequenceSize ; ++i) {
        bots[k][i+4*sequenceSize].sequenceSize = sequenceSize;
        bots[k][i+4*sequenceSize].baseMutateChance = 1.2/sequenceSize;
        bots[k][i+4*sequenceSize].shieldStep = MS_MAXRANDSHIELDSTEP;
        for(int j = 0; j<sequenceSize; ++j) {
          bots[k][i+4*sequenceSize].deltaAngle[j] = -MAXDELTAANGLE + (i+1.0)/(poolSize-4.0*sequenceSize+1.0)*2.0*MAXDELTAANGLE;
          bots[k][i+4*sequenceSize].thrustValue[j] = 5;
        }
      }
    }
  }

  void resetInternalTimer() {
    internalStartTime = getCurrentTime();
  }

  int getInternalTimer() {
    return std::chrono::duration_cast<std::chrono::microseconds>(getCurrentTime() - internalStartTime).count();
  }

  int oneBotTournamentSelection() {
    int tempId, bot1;
    bot1 = fastRandInt(poolSize);
    for(int i = 1; i < RG_TOURNAMENTPOPULATION ; ++i) {
      tempId = fastRandInt(poolSize);
      if(botScores[tempId] > botScores[bot1]) {
        bot1 = tempId;
      }
    }
    return bot1;
  }

  int secondBotTournamentSelection(int bot1) {
    int bot2 = oneBotTournamentSelection();
    while(bot2 == bot1)
      bot2 = oneBotTournamentSelection();
    return bot2;
  }

  void evolveWeakestBot(double amplitude) {
    if(useCombinedCase) {
      if(fastRandDouble() < 0.8) {
        int iBot1 = oneBotTournamentSelection();
        bots[0][weakestBotIndex].becomeRandomMutationFromOne(bots[0][iBot1], amplitude);
        bots[1][weakestBotIndex].becomeRandomMutationFromOne(bots[1][iBot1], amplitude);
      } else {
        int iBot1 = oneBotTournamentSelection();
        int iBot2 = secondBotTournamentSelection(iBot1);
        bots[0][weakestBotIndex].becomeRandomMutationFromTwo(bots[0][iBot1], bots[0][iBot2]);
        bots[1][weakestBotIndex].becomeRandomMutationFromTwo(bots[1][iBot1], bots[1][iBot2]);
      }
      bots[0][weakestBotIndex].smallMutation();
      bots[1][weakestBotIndex].smallMutation();
    } else {
      if(fastRandDouble() < 0.8) {
        bots[0][weakestBotIndex].becomeRandomMutationFromOne(bots[0][oneBotTournamentSelection()], amplitude);
      } else {
        int iBot1 = oneBotTournamentSelection();
        bots[0][weakestBotIndex].becomeRandomMutationFromTwo(bots[0][iBot1], bots[0][secondBotTournamentSelection(iBot1)]);
      }
      bots[0][weakestBotIndex].smallMutation();
    }
  }

  GameAction getNextAction(Game &game, int shipId) {
    int step = 0;
    int timerMiniStep = 0;
    resetInternalTimer();
    int elapsedTime = getInternalTimer();

    double score;
    weakestBotIndex = 0;
    strongestBotIndex = 0;
    for(int i = 0; i < poolSize; ++i) {
      if(useCombinedCase) {
        gameSimulator.bots[0]= &bots[0][i];
        gameSimulator.bots[1]= &bots[1][i];
      } else
        gameSimulator.bots[shipId] = &bots[0][i];

      gameSimulator.simulGame = game;
      gameSimulator.initializeSimulation();
      if(useCombinedCase) {
        gameSimulator.simulateFutureSpecialA(1);
        score = gameSimulator.simulGame.evaluateCombinedSimulationTurnZero(myRamId, oppRunId, myRamBeacon, riskTimeout);
        gameSimulator.simulateFutureSpecialA(sequenceSize);
        score += gameSimulator.simulGame.evaluateCombinedSimulationTurnN(myRunId, myRamId, oppRunId, myRamBeacon, riskTimeout);
        if(bots[myRamId][i].shieldStep == 0) score -= COEFFEVAL_GLOBALRAM*SHIELDCOST;
        else score += 0.5*COEFFEVAL_ILIKETOGOFAST * limitThrust(bots[myRamId][i].thrustValue[0]);
        if(bots[myRunId][i].shieldStep == 0) score -= SHIELDCOST;
        else score += 2.0*COEFFEVAL_ILIKETOGOFAST * limitThrust(bots[myRunId][i].thrustValue[0]);
      } else {
        gameSimulator.simulateFuture(sequenceSize, shipId);
        score = gameSimulator.simulGame.evaluateSinglePlayerSimulation(shipId, gameSimulator.behavior);
        score += COEFFGETAWAYFROMRAM * gameSimulator.singlePlayerMinDistanceToRam;
        score += COEFFPRIORITEHIGHTHRUST * limitThrust(bots[0][i].thrustValue[0]);
      }

      botScores[i] = score;
      if(botScores[i] < botScores[weakestBotIndex]) {
        weakestBotIndex = i;
      }
      if(botScores[i] > botScores[strongestBotIndex]) {
        strongestBotIndex = i;
      }
    }
    bestWeakestBotScore = botScores[weakestBotIndex];

    double amplitude;

    while(elapsedTime < timeBudget) {
      elapsedTime = getInternalTimer();
      if(elapsedTime > timerMiniStep) {
        if(useCombinedCase) LOGRECUIT("Step " << std::setw(5)<<step << " :  min " << std::setw(7) << bestWeakestBotScore << "   max " << std::setw(7) << botScores[strongestBotIndex]<< "   delta " << std::setw(7) << botScores[strongestBotIndex]-bestWeakestBotScore);
        timerMiniStep += RG_TIMERSTEPSIZE;
      }
      ++step;
      elapsedTime = getInternalTimer();

      amplitude = 1.0-elapsedTime/(double)timeBudget;

      if(botScores[strongestBotIndex] < (botScores[weakestBotIndex] + 0.3)) {
        if(useCombinedCase) {}
        for(int i = 0; i < poolSize; ++i) {
          if(i != strongestBotIndex) {
            botScores[i] -= 2000.0;
          }
        }
        bestWeakestBotScore -= 2000.0;
      }

      evolveWeakestBot(amplitude);

      if(useCombinedCase) {
        gameSimulator.bots[0]= &bots[0][weakestBotIndex];
        gameSimulator.bots[1]= &bots[1][weakestBotIndex];
        gameSimulator.simulGame = game;
        gameSimulator.initializeSimulation();
        gameSimulator.simulateFutureSpecialA(1);
        score = gameSimulator.simulGame.evaluateCombinedSimulationTurnZero(myRamId, oppRunId,  myRamBeacon, riskTimeout);
        gameSimulator.simulateFutureSpecialA(sequenceSize);
        score += gameSimulator.simulGame.evaluateCombinedSimulationTurnN(myRunId, myRamId, oppRunId, myRamBeacon, riskTimeout);
        if(bots[myRamId][weakestBotIndex].shieldStep == 0) score -= COEFFEVAL_GLOBALRAM*SHIELDCOST;
        else score += 0.5*COEFFEVAL_ILIKETOGOFAST * limitThrust(bots[myRamId][weakestBotIndex].thrustValue[0]);
        if(bots[myRunId][weakestBotIndex].shieldStep == 0) score -= SHIELDCOST;
        else score += 2.0*COEFFEVAL_ILIKETOGOFAST * limitThrust(bots[myRunId][weakestBotIndex].thrustValue[0]);
      } else {
        gameSimulator.bots[shipId] = &bots[0][weakestBotIndex];
        gameSimulator.simulGame = game;
        gameSimulator.initializeSimulation();
        gameSimulator.simulateFuture(sequenceSize,shipId);
        score = gameSimulator.simulGame.evaluateSinglePlayerSimulation(shipId, gameSimulator.behavior);
        score += COEFFGETAWAYFROMRAM * gameSimulator.singlePlayerMinDistanceToRam;
        score += COEFFPRIORITEHIGHTHRUST * limitThrust(bots[0][weakestBotIndex].thrustValue[0]);
      }

      if(score > botScores[strongestBotIndex]) {
        strongestBotIndex = weakestBotIndex;
      }

      if(score > bestWeakestBotScore) {
        botScores[weakestBotIndex] = score;
        weakestBotIndex = 0;
        score = botScores[0];
        for(int i = 1; i < poolSize ; ++i) {
          if(botScores[i] < score) {
            weakestBotIndex = i;
            score = botScores[i];
          }
        }
        bestWeakestBotScore = botScores[weakestBotIndex];
      } else {
        botScores[weakestBotIndex] = score;
      }
    }

    return bots[0][strongestBotIndex].getNextAction(game, shipId);
  }
};

/******************************************************************************
*
*     CLASS METABOT
*
*******************************************************************************/

class MetaBot {
public:
  RecuitGenetiqueBot rgBot;
  NeuralRunBot neuralRunBot;
  NeuralRamBot neuralRamBot;
  MoveSequenceBot moveSequenceBot[NPLAYERS*NSHIPS];

  int myRunId = 0;
  int myRamId = 1;
  int oppRunId = 2;
  int oppRamId = 3;
  int myRamBeacon = 2;

public:
  std::pair<GameAction,GameAction> getNextActions(Game &game) {

    rgBot.gameSimulator.defaultRunBot = &neuralRunBot;
    rgBot.gameSimulator.defaultRamBot = &neuralRamBot;
    for(int j = 0; j < NPLAYERS*NSHIPS; ++j) {
      rgBot.gameSimulator.bots[j] = &moveSequenceBot[j];
    }

    double d[4];
    for(int i = 0; i < 4; ++i) {
      d[i] = (norm(game.ships[i].pos-game.mapData->beacons[game.ships[i].nextBeacon].entryPointv2));
      d[i] += game.mapData->beacons[game.ships[i].nextBeacon].distToEnd[game.ships[i].lapNumber];
    }
    myRunId = (d[myRunId] < d[myRamId]+200) ? myRunId : myRamId;
    myRamId = (myRunId == 0) ? 1 : 0;
    oppRunId = (d[oppRunId] < d[oppRamId]+200) ? oppRunId : oppRamId;
    oppRamId = (oppRunId == 2) ? 3 : 2;

    if(!COOPERATIVERAMBEACON) {
      double myDistance,hisDistance;
      hisDistance = norm(game.ships[oppRunId].pos-game.mapData->beacons[game.ships[oppRunId].nextBeacon].pos);
      myDistance = norm(game.ships[myRamId].pos-game.mapData->beacons[game.ships[oppRunId].nextBeacon].pos);
      if(myDistance < hisDistance - 2200.0)
        myRamBeacon = game.ships[oppRunId].nextBeacon;
      else {
        hisDistance = norm(game.ships[oppRunId].pos-game.mapData->beacons[game.ships[oppRunId].nextBeacon].pos) + norm(game.mapData->beacons[(game.ships[oppRunId].nextBeacon+1)%game.mapData->nBeacons].pos-game.mapData->beacons[game.ships[oppRunId].nextBeacon].pos);
        myDistance = norm(game.ships[myRamId].pos-game.mapData->beacons[(game.ships[oppRunId].nextBeacon+1)%game.mapData->nBeacons].pos);
        if(myDistance < hisDistance - 2200.0)
          myRamBeacon = (1+game.ships[oppRunId].nextBeacon)%game.mapData->nBeacons;
        else
          myRamBeacon = (2+game.ships[oppRunId].nextBeacon)%game.mapData->nBeacons;
      }
    } else {
      double myDistance,hisDistance;
      hisDistance = norm(game.ships[myRunId].pos-game.mapData->beacons[game.ships[myRunId].nextBeacon].pos);
      myDistance = norm(game.ships[myRamId].pos-game.mapData->beacons[game.ships[myRunId].nextBeacon].pos);
      if(myDistance < hisDistance - 2200.0)
        myRamBeacon = game.ships[myRunId].nextBeacon;
      else
        myRamBeacon = (1+game.ships[myRunId].nextBeacon)%game.mapData->nBeacons;
    }

    if(game.mapData->bestShipLapNumber == 0 && game.mapData->bestShipCheckpointNumber == 0) {
      myRamBeacon = 1;
    }

    bool riskTimeout = (game.mapData->players[0].timeout > 140 && game.mapData->players[0].timeout >= game.mapData->players[1].timeout);

    rgBot.gameSimulator.setPolicyA(myRamId);
    rgBot.setupRecuitGenetiqueNotCombined(OPPONENTTIMEBUDGET, POOLSIZE, SEQUENCESIZE);
    rgBot.getNextAction(game,oppRunId);

    moveSequenceBot[oppRunId] = rgBot.bots[0][rgBot.strongestBotIndex];
    rgBot.gameSimulator.bots[oppRunId] = &moveSequenceBot[oppRunId];

    // Use heuristic ram bot to model opponent's rammer
    neuralRamBot.setTarget(myRunId);
    GameAction ramAction = neuralRamBot.getNextAction(game, oppRamId);
    moveSequenceBot[oppRamId].deltaAngle[0] = ramAction.deltaAngle;
    moveSequenceBot[oppRamId].thrustValue[0] = ramAction.thrust;
    moveSequenceBot[oppRamId].shieldStep = MS_MAXRANDSHIELDSTEP;
    rgBot.gameSimulator.bots[oppRamId] = &moveSequenceBot[oppRamId];

    rgBot.setupRecuitGenetiqueCombined(MYTIMEBUDGET, POOLSIZE, SEQUENCESIZE, myRunId, myRamId, oppRunId, oppRamId, myRamBeacon, riskTimeout);
    for(int k = 0; k< 2; ++k) {
      rgBot.bots[k][POOLSIZE-1].shieldStep = moveSequenceBot[k].shieldStep-1;
      for(int i = 0; i < SEQUENCESIZE-1; ++i) {
        rgBot.bots[k][POOLSIZE-1].deltaAngle[i] = moveSequenceBot[k].deltaAngle[i+1];
        rgBot.bots[k][POOLSIZE-1].thrustValue[i] = moveSequenceBot[k].thrustValue[i+1];
      }
    }

    rgBot.getNextAction(game,0);
    rgBot.bots[0][rgBot.strongestBotIndex].dump();
    rgBot.bots[1][rgBot.strongestBotIndex].dump();

    GameAction gameAction1 = rgBot.bots[0][rgBot.strongestBotIndex].getNextAction(game,0);
    GameAction gameAction2 = rgBot.bots[1][rgBot.strongestBotIndex].getNextAction(game,1);

    moveSequenceBot[0] = rgBot.bots[0][rgBot.strongestBotIndex];
    moveSequenceBot[1] = rgBot.bots[1][rgBot.strongestBotIndex];
    return std::make_pair(gameAction1, gameAction2);
  }
};

/******************************************************************************
*
*     CLASS ENVIRONMENT
*
*******************************************************************************/
class Environment {
public:

  MapData mapData;
  Game game;
  MetaBot metaBot;
  bool firstRoundInput = true;
  int environmentTurnNumber = 0;


  void gameInitialization(std::istream& iStream) {
    std::cerr.precision(6);
    iStream >> mapData.nLaps;
    iStream.ignore();
    iStream >> mapData.nBeacons;
    game.mapData = &mapData;
    for(int i = 0; i < mapData.nBeacons ; ++i) {
      iStream >> mapData.beacons[i].pos.x >> mapData.beacons[i].pos.y;
      iStream.ignore();
      mapData.beacons[i].speed = {0.0,0.0};
      mapData.beacons[i].tLastUpdate = 0.0;
      LOGINPUT("BEACON " << i << " : " << mapData.beacons[i].pos.x << " " << mapData.beacons[i].pos.y);
    }
    mapData.preCalculateBeaconStuff();
  }

  void roundInput(std::istream& iStream) {
    int tempNextBeacon;
    int tempSpeedx, tempSpeedy;
    int tempAngle;
    for(int i = 0; i < NPLAYERS*NSHIPS ; ++i) {
      iStream >> game.ships[i].pos.x >> game.ships[i].pos.y >> tempSpeedx >> tempSpeedy >> tempAngle >>tempNextBeacon;
      iStream.ignore();
#ifndef LOCAL_SERVER
      if(i >= 2) {
#endif
        game.ships[i].angle = Angle(tempAngle*PI/180.0);
#ifndef LOCAL_SERVER
      }
#endif

      game.ships[i].speed.x = tempSpeedx;
      game.ships[i].speed.y = tempSpeedy;
      game.ships[i].tLastUpdate = 0.0;

      if(tempNextBeacon != game.ships[i].nextBeacon) {
        game.mapData->players[i/2].timeout = 0;
        if(game.mapData->bestShipLapNumber == game.ships[i].lapNumber) {
          game.mapData->bestShipCheckpointNumber = std::max(game.mapData->bestShipCheckpointNumber, game.ships[i].nextBeacon);
        }
      }
      else
        game.mapData->players[i/2].timeout += 1;

      if(tempNextBeacon == 1 && game.ships[i].nextBeacon == 0)  {
        game.ships[i].lapNumber += 1;
        if(game.mapData->bestShipLapNumber < game.ships[i].lapNumber) {
          game.mapData->bestShipLapNumber = game.ships[i].lapNumber;
          game.mapData->bestShipCheckpointNumber = 0;
        }
      }

#ifndef LOCAL_SERVER
      if(firstRoundInput) {
        game.ships[i].angle = Angle(angleByvec2(game.mapData->beacons[1].pos-game.ships[i].pos));
      }
#endif

      game.ships[i].nextBeacon = tempNextBeacon;
    }
    firstRoundInput = false;
  }


  void roundOutput(std::ostream& oStream) {
    resetTimer();
    GameAction gameAction1,gameAction2;

    std::pair<GameAction, GameAction> gameActions = metaBot.getNextActions(game);
    gameAction1 = gameActions.first;
    gameAction2 = gameActions.second;

    updateShipAndOutput(oStream, 0, gameAction1);
    updateShipAndOutput(oStream, 1, gameAction2);

    environmentTurnNumber++;
  }

  void updateShipAndOutput(std::ostream& oStream, int shipId, GameAction& gameAction) {
    game.ships[shipId].angle += Angle(gameAction.deltaAngle);
    oStream  << (int) (game.ships[shipId].pos.x + round(vec2ByAngle(game.ships[shipId].angle.angleValue).x*100000))
             << " " << (int) (game.ships[shipId].pos.y + round(vec2ByAngle(game.ships[shipId].angle.angleValue).y*100000))
             << " ";
    if(gameAction.isShield)
      oStream << "SHIELD SHIELD" <<std::endl;
    else
      oStream << gameAction.thrust<<std::endl;
  }
};

/*****************************************************************************
*
*     INT MAIN
*
******************************************************************************/
int main() {

  fast_srand(211020151);
  Environment environment;

#ifdef LOCAL_SERVER
  std::fstream file;
  file.open("data/test_01.txt");
  std::stringstream iStream;
  iStream << file.rdbuf();
  file.close();
  int loop = 6;
#else
  int loop = 10000;
  std::istream& iStream = std::cin;
#endif
  environment.gameInitialization(iStream);
  do {
    environment.roundInput(iStream);
#ifdef COMPILE_FOR_CALLGRIND
    CALLGRIND_START_INSTRUMENTATION;
#endif
    environment.roundOutput(std::cout);
#ifdef COMPILE_FOR_CALLGRIND
    CALLGRIND_STOP_INSTRUMENTATION;
#endif
  } while(--loop);
  return 1;
}
