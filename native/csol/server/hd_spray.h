// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct edict_s;
struct TraceResult;
void CSOL_HDSprayActivate();
void CSOL_HDSprayReset();
void CSOL_HDSprayFrame();
bool CSOL_HDSprayCommand(edict_s*,const char*);
bool CSOL_HDSprayTrace(edict_s*,const TraceResult&);
