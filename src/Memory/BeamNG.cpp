///
/// Created by Anonymous275 on 1/21/22
/// Copyright (c) 2021-present Anonymous275 read the LICENSE file for more info.
///

#include "Memory/BeamNG.h"
#include "Memory/Memory.h"
#include "atomic_queue.h"

std::unique_ptr<atomic_queue<std::string, 1000>> RCVQueue, SendQueue;

int BeamNG::lua_open_jit_D(lua_State* State) {
   Memory::Print("Got lua State");
   GELua::State = State;
   RegisterGEFunctions();
   return OpenJITDetour->Original(State);
}

void BeamNG::EntryPoint() {
   RCVQueue     = std::make_unique<atomic_queue<std::string, 1000>>();
   SendQueue    = std::make_unique<atomic_queue<std::string, 1000>>();
   uint32_t PID = Memory::GetPID();
   auto status  = MH_Initialize();
   if (status != MH_OK)
      Memory::Print(std::string("MH Error -> ") + MH_StatusToString(status));
   Memory::Print("PID : " + std::to_string(PID));
   GELua::FindAddresses();
   /*GameBaseAddr = Memory::GetModuleBase(GameModule);
   DllBaseAddr = Memory::GetModuleBase(DllModule);*/
   OpenJITDetour = std::make_unique<Hook<def::lua_open_jit>>(
       GELua::lua_open_jit, lua_open_jit_D);
   OpenJITDetour->Enable();
   IPCFromLauncher = std::make_unique<IPC>(PID, 0x1900000);
   IPCToLauncher   = std::make_unique<IPC>(PID + 1, 0x1900000);
   CreateThread(nullptr, 0, LPTHREAD_START_ROUTINE(IPCSender), nullptr, 0,
                nullptr);
   IPCListener();
}

int Core(lua_State* L) {
   if (lua_gettop(L) == 1) {
      size_t Size;
      const char* Data = GELua::lua_tolstring(L, 1, &Size);
      // Memory::Print("Core -> " + std::string(Data) + " - " +
      // std::to_string(Size));
      std::string msg(Data, Size);
      BeamNG::SendIPC("C" + msg);
   }
   return 0;
}

int Game(lua_State* L) {
   if (lua_gettop(L) == 1) {
      size_t Size;
      const char* Data = GELua::lua_tolstring(L, 1, &Size);
      // Memory::Print("Game -> " + std::string(Data) + " - " +
      // std::to_string(Size));
      std::string msg(Data, Size);
      BeamNG::SendIPC("G" + msg);
   }
   return 0;
}

int LuaPop(lua_State* L) {
   std::string MSG;
   if (RCVQueue->try_pop(MSG)) {
      GELua::lua_push_fstring(L, "%s", MSG.c_str());
      return 1;
   }
   return 0;
}

void BeamNG::RegisterGEFunctions() {
   Memory::Print("Registering GE Functions");
   GELuaTable::Begin(GELua::State);
   GELuaTable::InsertFunction(GELua::State, "Core", Core);
   GELuaTable::InsertFunction(GELua::State, "Game", Game);
   GELuaTable::InsertFunction(GELua::State, "try_pop", LuaPop);
   GELuaTable::End(GELua::State, "MP");
   Memory::Print("Registered!");
}

void BeamNG::SendIPC(const std::string& Data) {
   if (SendQueue->size() < 800 || !RCVQueue->empty()) { SendQueue->push(Data); }
}

void BeamNG::IPCListener() {
   int TimeOuts = 0;
   while (TimeOuts < 20) {
      IPCFromLauncher->receive();
      if (!IPCFromLauncher->receive_timed_out()) {
         TimeOuts = 0;
         RCVQueue->push(IPCFromLauncher->msg());
         IPCFromLauncher->confirm_receive();
      } else TimeOuts++;
   }
   Memory::Print("IPC Listener System shutting down");
}

uint32_t BeamNG::IPCSender(void* LP) {
   std::string result;
   int TimeOuts = 0;
   while (TimeOuts < 20) {
      if (SendQueue->try_pop(result)) {
         IPCToLauncher->send(result);
         if (!IPCToLauncher->send_timed_out()) TimeOuts = 0;
         else TimeOuts++;
      }
   }
   Memory::Print("IPC Sender System shutting down");
   return 0;
}