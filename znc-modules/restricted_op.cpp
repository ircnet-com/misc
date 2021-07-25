/*
 * Restricted Op v1.0
 *
 * -!- Irssi: Starting query in IRCnet with *restricted_op
 * <admin> set
 * <*restricted_op> +----------------------+-----------------+
 * <*restricted_op> | Variable             | Value           |
 * <*restricted_op> +----------------------+-----------------+
 * <*restricted_op> | blocked_arg_modes    | eIR             |
 * <*restricted_op> | blocked_modes        | oklin           |
 * <*restricted_op> | channels             | #chan1 #chan2   |
 * <*restricted_op> | channels::patrick    | #chan3 #chan4   |
 * <*restricted_op> | dont_kick_ops        | true            |
 * <*restricted_op> +----------------------+-----------------+
 *
 * Explanation:
 * 1. user "patrick" cannot do the things explained below on #chan3 #chan4, but everywhere else ("channel::$user" overwrites "channels"!)
 * 2. all other users cannot do the things explained below on #chan1 #chan2, but everywhere else
 * 3. "MODE #channel +/-eIR <parameter>" is not allowed, while "MODE #channel +/-eIR" without parameter is allowed
 * 4. "MODE #channel +/-oklin [parameter]" is not allowed, no matter if the mode has a parameter or not
 *
 * Illegal attempts are reported by broadcast notice to admins and saved in moddata/restricted_op/restricted_op.log
 *
 * NOTE: This module parses only user input. Other modules and DefaultChanModes can bypass this module!
 *
 * Thanks for testing: matrix
 *
 * Copyright (C) 2009 IRCnet.com team
 */

#include <znc/Modules.h>
#include <znc/FileUtils.h>
#include <znc/IRCNetwork.h>
#include <znc/IRCSock.h>
#include <znc/Chan.h>
#include <znc/User.h>

using std::string;
using std::vector;

class CRestrictedOpMod : public CModule {
public:
    MODCONSTRUCTOR(CRestrictedOpMod) {
  }

  virtual ~CRestrictedOpMod() {
    Log("Logging ended.", false);
  }

  virtual EModRet OnUserRaw(CString& sLine) {
    CString cmd=sLine.Token(0).AsUpper();
    CString aChan, aNick;
    vector<CString> modeArgs;
    vector<CString>::iterator arg_iter;

    int chan_idx=0, nick_idx=0;

    if(cmd == "MODE") {
      CString channels=sLine.Token(1);
      CString modes=sLine.Token(2);
      CString anArg;
      int argIdx=3;
      char *currentMode, sign='+';

      anArg=sLine.Token(argIdx++, false);

      while(!anArg.empty()) {
        modeArgs.push_back(anArg);
        anArg=sLine.Token(argIdx++, false);
      }

      // parse #chan1,#chan2,..
      aChan=channels.Token(chan_idx++, false, ",").AsLower();

      while(!aChan.empty()) {
        if(!IsLockedChan(aChan)) {
          aChan=channels.Token(chan_idx++, false, ",").AsLower();
          continue;
        }

        if(modes.empty())
          return CONTINUE;

        currentMode=(char*) modes.c_str();
        arg_iter=modeArgs.begin();

        while(*currentMode) {
          if(*currentMode == '+' || *currentMode == '-') {
            sign=*currentMode;
          }

          else {
            if(!modeArgs.empty()) {
              for(unsigned int idx2=0; idx2 < blocked_arg_modes.length(); idx2++) {
                if(blocked_arg_modes[idx2] == *currentMode) {
                  PutModNotice("You cannot change mode '" + CString(blocked_arg_modes[idx2]) + "' on " + aChan);
                  Log("Illegal mode from " + m_pUser->GetUserName() + " '" + sLine + "'");
                  return HALT;
                }
              }
            }

            for(unsigned int idx2=0; idx2 < blocked_modes.length(); idx2++) {
              if(blocked_modes[idx2] == *currentMode) {
                PutModNotice("You cannot change mode '" + CString(blocked_modes[idx2]) + "' on " + aChan);
                Log("Illegal mode from " + m_pUser->GetUserName() + " '" + sLine + "'");
                return HALT;
              }
            }

            if(ChanModeRequiresArgument(sign, *currentMode) && arg_iter != modeArgs.end())
              arg_iter++;
          }

          currentMode++;

          // reached last mode but there can still be still args
          // make sure modes strings such as "+m +t +p +i" are parsed fully
          if(!*currentMode && arg_iter != modeArgs.end()) {
            currentMode=(char*) (*arg_iter).c_str();
            arg_iter++;
          }
        }

        aChan=channels.Token(chan_idx++, false, ",").AsLower();
      }
    }

    else if(cmd == "KICK") {
      // disallow to kick other ops
      if(!GetNV("dont_kick_ops").ToBool())
        return CONTINUE;

      CString channels=sLine.Token(1);
      CString nicks=sLine.Token(2, false);

      // parse #chan1,#chan2,..
      aChan=channels.Token(chan_idx++, false, ",").AsLower();

      while(!aChan.empty()) {
        if(!IsLockedChan(aChan)) {
          aChan=channels.Token(chan_idx++, false, ",");
          continue;
        }

        CChan* chan=GetNetwork()->FindChan(aChan);

        if(chan) {
          // parse nick1,nick2,..
          aNick=nicks.Token(nick_idx++, false, ",");

          while(!aNick.empty()) {
            CNick* pNick = chan->FindNick(aNick);

            if(pNick) {
              if(pNick->HasPerm(CChan::Op)) {
                PutModNotice("You cannot kick ops on " + aChan);
                Log("Illegal kick from " + m_pUser->GetUserName() + " '" + sLine + "'");
                return HALT;
              }
            }

            aNick=nicks.Token(nick_idx++, false, ",");
          }
        }

        aChan=channels.Token(chan_idx++, false, ",");
      }
    }

    else if(cmd == "PRIVMSG") {
      // try to avoid loading modules
      if(GetUser()->IsAdmin())
        return CONTINUE;

      CString targets=sLine.Token(1).AsLower();
      CString msg=sLine.Token(2).AsLower();

      // parse target1,target2,..
      aNick=targets.Token(nick_idx++, false, ",");

      while(!aNick.empty()) {
        if(aNick == "*status" && msg.WildCmp("*loadmod*")) {
          PutModNotice("You cannot load modules");
          Log("Illegal msg from " + m_pUser->GetUserName() + " '" + sLine + "'");
          return HALT;
        }

        aNick=targets.Token(nick_idx++, false, ",");
      }
    }

    return CONTINUE;
  }

  virtual bool OnLoad(const CString& sArgs, CString& sMessage) {
    blocked_arg_modes=GetNV("blocked_arg_modes");
    blocked_modes=GetNV("blocked_modes");
    CString channels=GetNV("channels");
    CString tmp;

    if(blocked_arg_modes.empty()) {
      blocked_arg_modes="eIR";
      SetNV("blocked_arg_modes", blocked_arg_modes);
    }

    if(blocked_modes.empty()) {
      blocked_modes="oklin";
      SetNV("blocked_modes", blocked_modes);
    }

    if(channels.empty())
      SetNV("channels", "#test1 #test2 #test3");

    if((tmp=GetNV("dont_kick_ops")).empty())
      SetNV("dont_kick_ops", CString(true));

    m_sLogFile = GetSavePath() + "/restricted_op.log";
    Log("Logging started.", false);
    return true;
  }

  virtual void OnModCommand(const CString& sLine) {
    CTable Table;

    if(!GetUser()->IsAdmin()) {
      PutModule("Access denied");
      return;
    }

    CString cmd=sLine.Token(0).AsUpper();

    if(cmd == "SET") {
      bool ok=false;
      CString var=sLine.Token(1).AsLower();
      CString val=sLine.Token(2, true);

      Table.AddColumn("Variable");
      Table.AddColumn("Value");
 
      if(var.empty()) {
        for(MCString::iterator it=BeginNV(); it != EndNV(); it++) {
          Table.AddRow();
          Table.SetCell("Variable", it->first);
          Table.SetCell("Value", it->second);
        }

        PutModule(Table);
      }

      else {
        if(var == "blocked_arg_modes") {
          if(!val.empty()) {
            blocked_arg_modes=val;
            SetNV("blocked_arg_modes", blocked_arg_modes);
          }

          ok=true;
        }

        else if(var == "blocked_modes") {
          if(!val.empty()) {
            blocked_modes=val;
            SetNV("blocked_modes", blocked_modes);
          }

          ok=true;
        }

        else if(var == "channels") {
          if(!val.empty()) {
            SetNV("channels", val);
          }

          ok=true;
        }

        else if(var.WildCmp("channels::?*")) {
          if(!val.empty())
            SetNV(var, val);

          ok=true;
        }

        else if(var == "dont_kick_ops") {
          if(!val.empty()) 
            SetNV(var, val);

          ok=true;
        }

        if(ok) {
          Table.AddRow();
          Table.SetCell("Variable", var);
          Table.SetCell("Value", GetNV(var));
          PutModule(Table);
        }

        else
          PutModule("Unknown variable");
      }
    } // cmd == "SET"
  }

  bool ChanModeRequiresArgument(char sign, char mode)
  {
    CIRCSock::EChanModeArgs type;
    CString chan_status_flags=GetNetwork()->GetIRCSock()->GetPermModes();

    if(!mode)
        return false;

    // 'o', 'v', ..
    if(chan_status_flags.find(mode) != ::string::npos)
        return true;

    type=GetNetwork()->GetIRCSock()->GetModeType(mode);

    if(type == CIRCSock::ListArg)
      return true;

    if(type == CIRCSock::HasArg)
      return true;
    
    if(type == CIRCSock::ArgWhenSet) {
      if(sign == '+')
        return true;
      else
        return false;
    }

    if(type == CIRCSock::NoArg)
      return false;

    return false;
  }


  bool IsLockedChan(CString &chan) {
    // "channel::$user" overwrites "channel"
    CString val=GetNV("channels::" + m_pUser->GetUserName().AsLower());
    CString arg;
    int i=0;

    if(val.empty())
      val=GetNV("channels");

    arg=val.Token(i++).AsLower();

    while(!arg.empty()) {
      if(chan == arg)
        return true;

      arg=val.Token(i++).AsLower();
    }

    return false;
  }

  void Log(CString sLine, bool sBroadcast=true) {
    // Notify all admins that are logged in
    if(sBroadcast) {
        CZNC::Get().Broadcast(sLine, true, nullptr, nullptr);
    }

    // log to file
    time_t curtime;
    tm* timeinfo;
    char buf[23];

    time(&curtime);
    timeinfo = localtime(&curtime);
    strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S] ",timeinfo);

    CFile LogFile(m_sLogFile);

    if (LogFile.Open(O_WRONLY | O_APPEND | O_CREAT)) {
        LogFile.Write(buf + sLine + "\n");
    }
    else {
        CZNC::Get().Broadcast("Failed to write to [" + m_sLogFile + "]: " + strerror(errno), true, nullptr, nullptr);
    }
  }

  CString blocked_arg_modes;
  CString blocked_modes;
  CString m_sLogFile;
  CString m_sIdent;
  CString m_sHost;
};

GLOBALMODULEDEFS(CRestrictedOpMod, "Restricted OP v1.0")
