#include <EEPROM.h>
#include <Keypad.h>
#include <LiquidCrystal.h>

#include "Chassis.h"
#include "DisDetectors.hpp"
#include "Input.h"
#include "OpenMV.h"
#include "Output.h"
#include "Recorder.h"

char buf[256];

// f, r1, r2, l1, l2
#define DISNUM 5
DisDetectors<DISNUM> dis;
unsigned char disPins[DISNUM][2] = {
    {24, 25}, {26, 27}, {28, 29}, {30, 31}, {32, 33}};

InfoData info;
unsigned char rWheel = 255, lWheel = 255;

const double MINTURNDIS = 20.00;

bool unready() { return info.photoDis < 0 || info.turnDis < 0; }
// for the running task

double avgDisRight() { return (dis[1] + dis[2]) / 2; }

double avgDisLeft() { return (dis[3] + dis[4]) / 2; }

double avgSideWidth() { return (avgDisRight() + avgDisLeft()) / 2; }

double turnOuterRadius() { return dis[0] + info.l - avgSideWidth(); }

double turnInnerRadius() { return turnOuterRadius() - info.w; }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void slightAdjust(int front, int back, int s1, int &rspeed, int &lspeed) //需要输入 左侧的 front和back距离传感器的编号
{
  int disdiff;
  do
  {
    disdiff = dis[front] - dis[back];         //记录左侧当前距离差
    chasiss::state.write(rspeed--, lspeed--); //两侧降速1
    chasiss::state.move();
    //delay
  } while (abs(dis[front] - dis[back]) < abs(disdiff) && abs(dis[front] - dis[back]) > s1); //如果距离差变小并且仍然需要调
}

void dramaticAdjust(int front, int back, int s1, int &rspeed, int &lspeed)
{
  if (dis[front] - dis[back] > 0)
  {
    while (dis[front] - dis[back] > s1)
    {
      chasiss::state.write(rspeed, lspeed--); //左马达不断降速
      chasiss::state.move();
    }
  }
  else
  {
    while (abs(dis[front] - dis[back]) > s1)
    {
      chasiss::state.write(rspeed--, lspeed); //右马达不断降速
      chasiss::state.move();
    }
  }
}

void goStraight()
{

  int s1, s2; //数值有待确定
  int rspeed = 255;
  int lspeed = 255;
  int disdiff = 0;
  chasiss::state.write(rspeed, lspeed);

  while (1)
  {
    chasiss::state.move();
    if (abs(dis[3] - dis[4] > s1)) //如果车身一侧有偏离有偏离
    {
      //先尝试微调
      slightAdjust(3, 4, s1, rspeed, lspeed);
      rspeed = 255; //恢复全速
      lspeed = 255; //恢复全速
      chasiss::state.write(rspeed, lspeed);
      chasiss::state.move();

      if (abs(dis[3] - dis[4]) > s1))//如果微调失败则剧烈调整
        {
          dramaticAdjust(3, 4, rspeed, lspeed);
          rspeed = 255; //恢复全速
          lspeed = 255; //恢复全速
          chasiss::state.write(rspeed, lspeed);
          chasiss::state.move();
        }
    }

    if (abs(avgDisRight() - avgDisLeft()) > s2) //如果左右两侧差距过大
    {
      if (avgDisRight() - avgDisLeft() > 0) //右侧距离太大，需要向右调整
      {
        do
        {
          chasiss::state.write(rspeed--, lspeed);
          chasiss::state.move();
        } while (avgDisRight() - avgDisLeft() > s2);
        rspeed = 255; //恢复全速
        lspeed = 255; //恢复全速
        chasiss::state.write(rspeed, lspeed);
        chasiss::state.move();
      }
      else
      {
        do
        {
          chasiss::state.write(rspeed, lspeed--);
          chasiss::state.move();
        } while (avgDisRight() - avgDisLeft() > s2);
        rspeed = 255; //恢复全速
        lspeed = 255; //恢复全速
        chasiss::state.write(rspeed, lspeed);
        chasiss::state.move();
      }
    }
  }
  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  bool withinError(double dis1, double dis2, double Merror)
  {
    double error = dis1 - dis2;
    double abserror = error > 0 ? error : -error;
    return abserror < Merror;
  }

  bool isTurningEnd(int dir, bool test = false, long stTime = 0)
  {
    bool isEnd = false;
    if (withinError(dis[1 + dir * 2], dis[2 + dir * 2], 0.5))
      isEnd = true;
    if (test)
    {
      if (millis() - stTime > 4000)
        isEnd = true;
    }
    return isEnd;
  }

  void doControlTurn(int dir, bool test = false)
  {
    long stTime = millis();
    double ratio = turnOuterRadius() / turnInnerRadius();
    if (dir)
    {
      rWheel *= ratio;
    }
    else
    {
      lWheel *= ratio;
    }
    Chassis::state().write(rWheel, lWheel);
    while (1)
    {
      if (isTurningEnd(dir, test, stTime))
        return;
      Chassis::state().move();
    }
  }

  void doFreeTurn()
  {
    while (dis[0] < MINTURNDIS)
      Chassis::state().write(255, 255);
    int dir = dis[1] > dis[3] ? dir = 1 : dir = 0;
    if (dir)
    {
      Chassis::state().write(0, 255);
    }
    else
    {
      Chassis::state().write(255, 0);
    }
    while (!withinError(dis[1 + dir * 2], dis[2 + dir * 2], 1))
      Chassis::state().move();
  }

  void doRun()
  {
    bool sd = false, st = false;
    int dir = -1;
    while (1)
    {
      if (dis[0] < info.photoDis && dis[0] > info.turnDis && sd == false)
      {
        OpenMV::startDetect();
        sd = true;
      }
      if (dis[0] < info.turnDis && st == false)
      {
        sd = false;
        dir = OpenMV::getDir();
        OpenMV::endDetect();
        st = true;
      }
      if (st)
      {
        if (dir == -1)
        {
          doFreeTurn();
        }
        else
        {
          doControlTurn(dir);
        }
        st = false;
      }
      goStraight();
      Chassis::state().move();
    }
  }

  void run()
  {
    Output::screen().clear();
    if (unready())
    {
      Output::screen().parse("{distance data is not collected!} d d c");
    }
    else
    {
      Output::screen().parse("{distance data is collected} d c");
    }
    Output::screen().parse("{ready to run, press A to continue}");
    while (Input::device().getKey() != 'A')
      continue;
    doRun();
  }

  void configureDisTurn()
  {
    Output::screen().parse(
        "c p{now begin to configure the distance where this car would start to "
        "turn.} c");
    double d;
    int dir;
    char key;
    while (1)
    {
      Output::screen().parse(
          "c p{put the car at a desired distance and press C to "
          "continue}");
      while (Input::device().getKey() != 'C')
        continue;
      Output::screen().parse("c {start testing}");
      d = dis[0];
      OpenMV::startDetect();
      delay(1000);
      dir = OpenMV::getDir();
      OpenMV::endDetect();
      doControlTurn(dir, true);
      Output::screen().print("is everything okay?", 1);
      Output::screen().print("A: agian. B: save", 3);
      while ((key = Input::device().getKey()) == NO_KEY)
        continue;
      if (key == 'A')
      {
        continue;
      }
      else
      {
        break;
      }
    }
    info.turnDis = d;
  }

  void configureDisPhoto()
  {
    Output::screen().parse(
        "c p{now begin to configure the distance where this car would start to "
        "photo and detect.} d");
    char key;
    int dir;
    double d;
    while (1)
    {
      Output::screen().parse(
          "c p{put the car at a desired distance and press C to "
          "continue}");
      while (Input::device().getKey() != 'C')
        continue;
      Output::screen().parse("c {start testing}");
      d = dis[0];
      OpenMV::startDetect();
      delay(1000);
      dir = OpenMV::getDir();
      OpenMV::endDetect();
      Output::screen().parse("c {the Result: dis is }");
      Output::screen().print(d, 1);
      switch (dir)
      {
      case 0:
        Output::screen().print("left", 2);
        break;
      case 1:
        Output::screen().print("right", 2);
        break;
      case -1:
        Output::screen().print("failed", 2);
      default:
        break;
      }
      Output::screen().print("A: agian. B: save", 3);
      while ((key = Input::device().getKey()) == NO_KEY)
        continue;
      if (key == 'A')
      {
        continue;
      }
      else
      {
        break;
      }
    }
    info.photoDis = d;
  }

  void tune()
  {
    Output::screen().parse("c b{1. get disPhoto; 2. get disTurn;A to return}");
    char key;
    while (1)
    {
      key = Input::device().getKey();
      if (key != NO_KEY)
      {
        switch (key)
        {
        case '1':
          configureDisPhoto();
          break;
        case '2':
          configureDisTurn();
          break;
        case 'A':
          Recorder::disk().record(info);
          return;
        default:
          break;
        }
      }
    }
  }

  void reset()
  {
    Output::screen().parse("c {reset} d");
    Output::screen().parse(
        ("c p{you are going to reset the EEPROM.} c p {This process is "
         "not recoverable!} d"));
    char key;
    while ((key = Input::device().getKey()) == NO_KEY)
      continue;
    if (key == 'A')
      return;
    else
      for (int i = 0; i < EEPROM.length(); i++)
      {
        if (EEPROM.read(i) != 0)
          EEPROM.write(i, 0);
      }
    Output::screen().parse("c p {the EEPROM has been reset} d d");
  }

  void aboutpt(double v, const char *err)
  {
    if (v < 0)
    {
      Output::screen().clear();
      Output::screen().print(err);
      delay(1000);
    }
    else
    {
      Output::screen().print(v);
      delay(1000);
    }
  }

  void about()
  {
    Output::screen().parse(
        "c p {following are the data}  d c{l, w, sd, pd, td} d");
    aboutpt(info.l, "length undefined");
    aboutpt(info.w, "width undefined");
    aboutpt(info.sensorDis, "sensorDis undefined");
    aboutpt(info.photoDis, "photoDis undefined");
    aboutpt(info.turnDis, "turnDis undefined");
  }

  void ts()
  {
    int i;
    Output::screen().parse("c {use A to return}");
    while (1)
    {
      if (Serial.available())
      {
        Output::screen().parse("c {use A to return}");
        for (i = 0; Serial.available(); i++)
        {
          buf[i] = Serial.read();
          delay(20);
        }
        buf[i] = 0;
        Output::screen().print(buf, 1);
      }
      if (Input::device().getKey() == 'A')
        break;
    }
    Output::screen().parse("c {end}");
  }

  void tg()
  {
    bool p22 = false, p23 = false, p22t = false, p23t = false;
    Output::screen().parse("c {pin 22, 23 is used to test}");
    pinMode(22, INPUT);
    pinMode(23, INPUT);
    while (1)
    {
      p22 = digitalRead(22);
      p23 = digitalRead(23);
      if (p22 != p22t || p23 != p23t)
      {
        Output::screen().parse("c {pin 22, 23 is used to test. 22, 23}");
        Output::screen().print(p22, 2);
        Output::screen().print(p23, 3);
        p22t = p22;
        p23t = p23;
      }
      if (Input::device().getKey() == 'A')
        break;
    }
    Output::screen().parse("c {end}");
  }

  void tc()
  {
    char key;
    Output::screen().parse("c {test commands, 1-start, 2-dir, 3-end}");
    while (1)
    {
      key = Input::device().getKey();
      if (key != NO_KEY)
        switch (key)
        {
        case '1':
          OpenMV::startDetect();
          Output::screen().print("start", 3);
          break;
        case '2':
          Output::screen().print(OpenMV::getDir(), 3);
          break;
        case '3':
          OpenMV::endDetect();
          Output::screen().print("end", 3);
          break;
        default:
          Output::screen().parse("c {end}");
          return;
        }
    }
  }

  void debug()
  {
    char key;
    Output::screen().parse(
        "b{1. test Serial;2. test GPIO;3. test commands;4. quit;}");
    while (1)
    {
      if ((key = Input::device().getKey()) != NO_KEY)
      {
        switch (key)
        {
        case '1':
          ts();
          Output::screen().parse(
              "b{1. test Serial;2. test GPIO;3. test commands;4. quit;}");
          break;
        case '2':
          tg();
          Output::screen().parse(
              "b{1. test Serial;2. test GPIO;3. test commands;4. quit;}");
          break;
        case '3':
          tc();
          Output::screen().parse(
              "b{1. test Serial;2. test GPIO;3. test commands;4. quit;}");
          break;
        default:
          return;
        }
      }
    }
    return;
  }

#define MENU "b {1.run&2.tune;3.reset&4.about;5.debug&;}"

  void setup()
  {
    Serial.begin(9600);
    while (!Serial)
      continue;
    dis.attach(disPins);
    info = Recorder::disk().readRecord();
    Output::screen().parse(MENU);
  }

  void loop()
  {
    char key = Input::device().getKey();
    switch (key)
    {
    case '1':
      run();
      Output::screen().parse("c {quit} d" MENU);
      break;
    case '2':
      tune();
      Output::screen().parse("c {quit} d" MENU);
      break;
    case '3':
      reset();
      Output::screen().parse("c {quit} d" MENU);
      break;
    case '4':
      about();
      Output::screen().parse("c {quit} d" MENU);
      break;
    case '5':
      debug();
      Output::screen().parse("c {quit} d" MENU);
    default:
      break;
    }
  }
