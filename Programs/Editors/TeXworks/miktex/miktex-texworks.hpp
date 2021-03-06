/* miktex-texworks.hpp:

   Copyright (C) 2015-2016 Christian Schenk

   This file is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.
   
   This file is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this file; if not, write to the Free Software
   Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
   USA.  */

#include <string>
#include <vector>

#include <miktex/Trace/TraceCallback>

class MiKTeX_TeXworks :
  public MiKTeX::Trace::TraceCallback
{
public:
  int Run(int(*Main)(int argc, char *argv[]), int argc, char *argv[]);
  
private:
  bool isLog4cxxConfigured = false;

private:
  std::vector<MiKTeX::Trace::TraceCallback::TraceMessage> pendingTraceMessages;

public:
  void MIKTEXTHISCALL Trace(const MiKTeX::Trace::TraceCallback::TraceMessage & traceMessage) override;

private:
  void FlushPendingTraceMessages();

private:
  void TraceInternal(const MiKTeX::Trace::TraceCallback::TraceMessage & traceMessage);

private:
  void Sorry()
  {
    return Sorry("");
  }

private:
  void Sorry(std::string reason);
};
