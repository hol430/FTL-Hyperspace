#include "Global.h"
#include "LuaLibScript.h"
#include "InternalEvents.h"
#include "swigluarun.h"

#include <stack>

/** All the hook code for the various InternalEvents belongs here **/

enum class AIPhase
{
    SHIP,
    POWER,
    CREW,
    COMBAT,
    WEAPONS,
    MIND,
    ARTILLERY
};

struct AIContextFrame
{
    AIPhase phase;
    ShipAI *shipAI;
    CrewAI *crewAI;
    CombatAI *combatAI;
    ArtillerySystem *artillery;
};

static thread_local std::vector<AIContextFrame> g_aiContextStack;
static thread_local int g_aiActionCallbackDepth = 0;

class ScopedAIContext
{
public:
    ScopedAIContext(AIPhase phase, ShipAI *shipAI = nullptr, CrewAI *crewAI = nullptr,
                    CombatAI *combatAI = nullptr, ArtillerySystem *artillery = nullptr)
    {
        g_aiContextStack.push_back({phase, shipAI, crewAI, combatAI, artillery});
    }

    ~ScopedAIContext()
    {
        g_aiContextStack.pop_back();
    }
};

class ScopedAIActionCallback
{
public:
    ScopedAIActionCallback() { ++g_aiActionCallbackDepth; }
    ~ScopedAIActionCallback() { --g_aiActionCallbackDepth; }
};

static ShipAI *GetCurrentShipAI()
{
    for (auto it = g_aiContextStack.rbegin(); it != g_aiContextStack.rend(); ++it)
    {
        if (it->phase == AIPhase::SHIP && it->shipAI) return it->shipAI;
    }
    return nullptr;
}

static CrewAI *GetCurrentCrewAI()
{
    for (auto it = g_aiContextStack.rbegin(); it != g_aiContextStack.rend(); ++it)
    {
        if (it->phase == AIPhase::CREW && it->crewAI) return it->crewAI;
    }
    return nullptr;
}

static CombatAI *GetCurrentCombatAI()
{
    for (auto it = g_aiContextStack.rbegin(); it != g_aiContextStack.rend(); ++it)
    {
        if (it->phase == AIPhase::COMBAT && it->combatAI) return it->combatAI;
    }
    return nullptr;
}

HOOK_METHOD(CApp, OnLoop, () -> void)
{
    LOG_HOOK("HOOK_METHOD -> CApp::OnLoop -> Begin (InternalEvents.cpp)\n")
    super();
    Global::GetInstance()->getLuaContext()->getLibScript()->call_on_internal_event_callbacks(InternalEvents::ON_TICK);
}

HOOK_METHOD(MainMenu, Open, () -> bool)
{
    LOG_HOOK("HOOK_METHOD -> MainMenu::Open -> Begin (InternalEvents.cpp)\n")
    bool ret = super();
    Global::GetInstance()->getLuaContext()->getLibScript()->call_on_internal_event_callbacks(InternalEvents::MAIN_MENU);
    return ret;
}

HOOK_METHOD(SpaceManager, DangerousEnvironment, () -> bool)
{
    LOG_HOOK("HOOK_METHOD -> SpaceManager::DangerousEnvironment -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();
    bool res = super();

    lua_pushboolean(context->GetLua(), res);
    if (context->getLibScript()->call_on_internal_event_callbacks(InternalEvents::DANGEROUS_ENVIRONMENT, 1, 1) == 1)
    {
        res = lua_toboolean(context->GetLua(), -1);
        lua_pop(context->GetLua(), 2);
    }
    else // No return from callback
    {
        lua_pop(context->GetLua(), 1);
    }

    return res;
}

static std::string g_customHazardText = "";
HOOK_METHOD_PRIORITY(StarMap, GetLocationText, -100, (const Location* loc) -> std::string)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> StarMap::GetLocationText -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    SWIG_NewPointerObj(context->GetLua(), loc, context->getLibScript()->types.pLocation, 0);
    if (context->getLibScript()->call_on_internal_event_callbacks(InternalEvents::GET_BEACON_HAZARD, 1, 1) == 1 && lua_isstring(context->GetLua(), -1))
    {
        int originalEnv = loc->event->environment;
        g_customHazardText = lua_tostring(context->GetLua(), -1);
        loc->event->environment = 1;
        std::string ret = super(loc);
        loc->event->environment = originalEnv;
        g_customHazardText = "";
        lua_pop(context->GetLua(), 2);
        return ret;
    }
    else // No return from callback
    {
        lua_pop(context->GetLua(), 1);
        return super(loc);
    }
}
HOOK_METHOD(TextLibrary, GetText, (const std::string& name, const std::string& lang) -> std::string)
{
    LOG_HOOK("HOOK_METHOD -> TextLibrary::GetText -> Begin (InternalEvents.cpp)\n")
    return (!g_customHazardText.empty() && name == "map_asteroid_loc") ? g_customHazardText : super(name, lang);
}
HOOK_METHOD(StarMap, OnRender, () -> void)
{
    LOG_HOOK("HOOK_METHOD -> StarMap::OnRender -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    std::stack<std::pair<int, int>> originalEnvs;
    for (int i = 0; i < locations.size(); ++i)
    {
        Location *loc = locations[i];

        SWIG_NewPointerObj(context->GetLua(), loc, context->getLibScript()->types.pLocation, 0);
        if (context->getLibScript()->call_on_internal_event_callbacks(InternalEvents::GET_BEACON_HAZARD, 1, 1) == 1)
        {
            originalEnvs.push({i, loc->event->environment});
            loc->event->environment = 1;
            lua_pop(context->GetLua(), 2);
        }
        else // No return from callback
        {
            lua_pop(context->GetLua(), 1);
        }

    }

    super();

    while (!originalEnvs.empty())
    {
        locations[originalEnvs.top().first]->event->environment = originalEnvs.top().second;
        originalEnvs.pop();
    }
}

static GL_Color g_flashColor = GL_Color(0.f, 0.f, 0.f, 0.f);
HOOK_METHOD(SpaceManager, GetFlashOpacity, () -> float)
{
    LOG_HOOK("HOOK_METHOD -> SpaceManager::GetFlashOpacity -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    float opacity = super();
    lua_pushnumber(context->GetLua(), opacity);
    if (context->getLibScript()->call_on_internal_event_callbacks(InternalEvents::GET_HAZARD_FLASH, 1, 4) == 4)
    {
        g_flashColor.r = lua_tonumber(context->GetLua(), -4);
        g_flashColor.g = lua_tonumber(context->GetLua(), -3);
        g_flashColor.b = lua_tonumber(context->GetLua(), -2);
        g_flashColor.a = lua_tonumber(context->GetLua(), -1);
        opacity = g_flashColor.a;
        lua_pop(context->GetLua(), 5);
    }
    else // No return from callback
    {
        lua_pop(context->GetLua(), 1);
    }

    return opacity;
}
HOOK_STATIC(CSurface, GL_RenderPrimitiveWithColor, (GL_Primitive *primitive, GL_Color color) -> void)
{
    LOG_HOOK("HOOK_STATIC -> CSurface::GL_RenderPrimitiveWithColor -> Begin (InternalEvents.cpp)\n")

    if (g_flashColor.a > 0.f)
    {
        g_flashColor.a = color.a;
        super(primitive, g_flashColor);
        g_flashColor.a = 0.f;
    }
    else
    {
        super(primitive, color);
    }
}

HOOK_METHOD(CApp, OnKeyDown, (SDLKey key) -> void)
{
    LOG_HOOK("HOOK_METHOD -> CApp::OnKeyDown -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    lua_pushinteger(context->GetLua(), key);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::ON_KEY_DOWN, 1, 0);
    lua_pop(context->GetLua(), 1);

    if (!preempt) super(key);
}

HOOK_METHOD(CApp, OnKeyUp, (SDLKey key) -> void)
{
    LOG_HOOK("HOOK_METHOD -> CApp::OnKeyUp -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    lua_pushinteger(context->GetLua(), key);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::ON_KEY_UP, 1, 0);
    lua_pop(context->GetLua(), 1);

    if (!preempt) super(key);
}

HOOK_METHOD(CApp, OnMouseMove, (int x, int y, int xdiff, int ydiff, bool holdingLMB, bool holdingRMB, bool holdingMMB) -> void)
{
    LOG_HOOK("HOOK_METHOD -> CApp::OnMouseMove -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    lua_pushinteger(context->GetLua(), x);
    lua_pushinteger(context->GetLua(), y);
    lua_pushinteger(context->GetLua(), xdiff);
    lua_pushinteger(context->GetLua(), ydiff);
    lua_pushboolean(context->GetLua(), holdingLMB);
    lua_pushboolean(context->GetLua(), holdingRMB);
    lua_pushboolean(context->GetLua(), holdingMMB);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::ON_MOUSE_MOVE, 7, 0);
    lua_pop(context->GetLua(), 7);

    if (!preempt) super(x, y, xdiff, ydiff, holdingLMB, holdingRMB, holdingMMB);
}

HOOK_METHOD(CApp, OnLButtonDown, (int x, int y) -> void)
{
    LOG_HOOK("HOOK_METHOD -> CApp::OnLButtonDown -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    lua_pushinteger(context->GetLua(), x);
    lua_pushinteger(context->GetLua(), y);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::ON_MOUSE_L_BUTTON_DOWN, 2, 0);
    lua_pop(context->GetLua(), 2);

    if (!preempt) super(x, y);
}

HOOK_METHOD(CApp, OnLButtonUp, (int x, int y) -> void)
{
    LOG_HOOK("HOOK_METHOD -> CApp::OnLButtonUp -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    lua_pushinteger(context->GetLua(), x);
    lua_pushinteger(context->GetLua(), y);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::ON_MOUSE_L_BUTTON_UP, 2, 0);
    lua_pop(context->GetLua(), 2);

    if (!preempt) super(x, y);
}

HOOK_METHOD(CApp, OnRButtonDown, (int x, int y) -> void)
{
    LOG_HOOK("HOOK_METHOD -> CApp::OnRButtonDown -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    lua_pushinteger(context->GetLua(), x);
    lua_pushinteger(context->GetLua(), y);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::ON_MOUSE_R_BUTTON_DOWN, 2, 0);
    lua_pop(context->GetLua(), 2);

    if (!preempt) super(x, y);
}

HOOK_METHOD_PRIORITY(CApp, OnRButtonUp, -100, (int x, int y) -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CApp::OnRButtonUp -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    lua_pushinteger(context->GetLua(), x);
    lua_pushinteger(context->GetLua(), y);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::ON_MOUSE_R_BUTTON_UP, 2, 0);
    lua_pop(context->GetLua(), 2);

    if (!preempt) super(x, y);
}

HOOK_METHOD(CApp, OnMButtonDown, (int x, int y) -> void)
{
    LOG_HOOK("HOOK_METHOD -> CApp::OnMButtonDown -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    lua_pushinteger(context->GetLua(), x);
    lua_pushinteger(context->GetLua(), y);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::ON_MOUSE_M_BUTTON_DOWN, 2, 0);
    lua_pop(context->GetLua(), 2);

    if (!preempt) super(x, y);
}

HOOK_METHOD_PRIORITY(CrewMember, OnLoop, -100, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CrewMember::OnLoop -> Begin (InternalEvents.cpp)\n")

    super();

    auto context = Global::GetInstance()->getLuaContext();

    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pCrewMember, 0);
    context->getLibScript()->call_on_internal_event_callbacks(InternalEvents::CREW_LOOP, 1);
    lua_pop(context->GetLua(), 1);
}
//Priority was necessary to make this run after the hook for calculating stuff with additionalPowerLoss, so user can do stuff like modify that for weapon effects here.
HOOK_METHOD_PRIORITY(ShipManager, OnLoop, -100, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> ShipManager::OnLoop -> Begin (InternalEvents.cpp)\n")
    super();

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipManager, 0);
    context->getLibScript()->call_on_internal_event_callbacks(InternalEvents::SHIP_LOOP, 1);
    lua_pop(context->GetLua(), 1);
}

HOOK_METHOD_PRIORITY(ShipAI, OnLoop, -10000, (bool hostile) -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> ShipAI::OnLoop -> Begin (InternalEvents.cpp)\n")

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipAI, 0);
    lua_pushboolean(context->GetLua(), hostile);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SHIP_AI_PRE, 2, 0);

    if (!preempt)
    {
        ScopedAIContext aiContext(AIPhase::SHIP, this);
        super(hostile);
    }

    lua_pushboolean(context->GetLua(), preempt);
    context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SHIP_AI_POST, 3, 0);
    lua_pop(context->GetLua(), 3);
}

HOOK_METHOD_PRIORITY(ShipAI, CheckPowerLevels, -10000, (bool hostile) -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> ShipAI::CheckPowerLevels -> Begin (InternalEvents.cpp)\n")

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipAI, 0);
    lua_pushboolean(context->GetLua(), hostile);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SHIP_AI_POWER_PRE, 2, 0);

    if (!preempt)
    {
        ScopedAIContext aiContext(AIPhase::POWER, this);
        super(hostile);
    }

    lua_pushboolean(context->GetLua(), preempt);
    context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SHIP_AI_POWER_POST, 3, 0);
    lua_pop(context->GetLua(), 3);
}

HOOK_METHOD_PRIORITY(CrewAI, OnLoop, -10000, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CrewAI::OnLoop -> Begin (InternalEvents.cpp)\n")

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pCrewAI, 0);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::CREW_AI_PRE, 1, 0);

    if (!preempt)
    {
        ScopedAIContext aiContext(AIPhase::CREW, nullptr, this);
        super();
    }

    lua_pushboolean(context->GetLua(), preempt);
    context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::CREW_AI_POST, 2, 0);
    lua_pop(context->GetLua(), 2);
}

HOOK_METHOD_PRIORITY(CombatAI, OnLoop, -10000, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CombatAI::OnLoop -> Begin (InternalEvents.cpp)\n")

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pCombatAI, 0);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_PRE, 1, 0);

    if (!preempt)
    {
        ScopedAIContext aiContext(AIPhase::COMBAT, nullptr, nullptr, this);
        super();
    }

    lua_pushboolean(context->GetLua(), preempt);
    context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_POST, 2, 0);
    lua_pop(context->GetLua(), 2);
}

HOOK_METHOD_PRIORITY(CombatAI, UpdateWeapons, -10000, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CombatAI::UpdateWeapons -> Begin (InternalEvents.cpp)\n")

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pCombatAI, 0);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_WEAPONS_PRE, 1, 0);

    if (!preempt)
    {
        ScopedAIContext aiContext(AIPhase::WEAPONS, nullptr, nullptr, this);
        super();
    }

    lua_pushboolean(context->GetLua(), preempt);
    context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_WEAPONS_POST, 2, 0);
    lua_pop(context->GetLua(), 2);
}

HOOK_METHOD_PRIORITY(CombatAI, UpdateMindControl, -10000, (bool hostile) -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CombatAI::UpdateMindControl -> Begin (InternalEvents.cpp)\n")

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pCombatAI, 0);
    lua_pushboolean(context->GetLua(), hostile);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_MIND_PRE, 2, 0);

    if (!preempt)
    {
        ScopedAIContext aiContext(AIPhase::MIND, nullptr, nullptr, this);
        super(hostile);
    }

    lua_pushboolean(context->GetLua(), preempt);
    context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_MIND_POST, 3, 0);
    lua_pop(context->GetLua(), 3);
}

HOOK_METHOD_PRIORITY(ShipManager, SetCloaked, -10000, (bool cloaked) -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> ShipManager::SetCloaked -> Begin (InternalEvents.cpp)\n")

    CombatAI *ai = GetCurrentCombatAI();
    if (!ai || ai->self != this || g_aiActionCallbackDepth > 0) return super(cloaked);

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), ai, context->getLibScript()->types.pCombatAI, 0);
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipManager, 0);
    lua_pushboolean(context->GetLua(), cloaked);
    bool preempt;
    {
        ScopedAIActionCallback callbackGuard;
        preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_CLOAK_PRE, 3, 1);
    }
    if (lua_isboolean(context->GetLua(), -1)) cloaked = lua_toboolean(context->GetLua(), -1);
    lua_pop(context->GetLua(), 3);

    if (!preempt) super(cloaked);

    SWIG_NewPointerObj(context->GetLua(), ai, context->getLibScript()->types.pCombatAI, 0);
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipManager, 0);
    lua_pushboolean(context->GetLua(), cloaked);
    lua_pushboolean(context->GetLua(), preempt);
    {
        ScopedAIActionCallback callbackGuard;
        context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_CLOAK_POST, 4, 0);
    }
    lua_pop(context->GetLua(), 4);
}

HOOK_METHOD_PRIORITY(HackingSystem, StartHacking, -10000, (ShipSystem *targetSystem) -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> HackingSystem::StartHacking -> Begin (InternalEvents.cpp)\n")

    CombatAI *ai = GetCurrentCombatAI();
    if (!ai || !ai->self || ai->self->hackingSystem != this || g_aiActionCallbackDepth > 0) return super(targetSystem);

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), ai, context->getLibScript()->types.pCombatAI, 0);
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->GetShipSystemType(SYS_HACKING), 0);
    SWIG_NewPointerObj(context->GetLua(), targetSystem, context->getLibScript()->GetShipSystemType(targetSystem ? targetSystem->iSystemType : -1), 0);
    bool preempt;
    {
        ScopedAIActionCallback callbackGuard;
        preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_HACK_START_PRE, 3, 1);
    }
    ShipSystem *replacement = nullptr;
    if (SWIG_isptrtype(context->GetLua(), -1) &&
        SWIG_IsOK(SWIG_ConvertPtr(context->GetLua(), -1, (void**)&replacement, context->getLibScript()->types.pShipSystem, 0)))
    {
        targetSystem = replacement;
    }
    lua_pop(context->GetLua(), 3);

    if (!preempt && targetSystem) super(targetSystem);

    SWIG_NewPointerObj(context->GetLua(), ai, context->getLibScript()->types.pCombatAI, 0);
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->GetShipSystemType(SYS_HACKING), 0);
    SWIG_NewPointerObj(context->GetLua(), targetSystem, context->getLibScript()->GetShipSystemType(targetSystem ? targetSystem->iSystemType : -1), 0);
    lua_pushboolean(context->GetLua(), preempt);
    {
        ScopedAIActionCallback callbackGuard;
        context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_HACK_START_POST, 4, 0);
    }
    lua_pop(context->GetLua(), 4);
}

HOOK_METHOD_PRIORITY(HackingSystem, InitiatePulse, -10000, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> HackingSystem::InitiatePulse -> Begin (InternalEvents.cpp)\n")

    CombatAI *ai = GetCurrentCombatAI();
    if (!ai || !ai->self || ai->self->hackingSystem != this || g_aiActionCallbackDepth > 0) return super();

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), ai, context->getLibScript()->types.pCombatAI, 0);
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->GetShipSystemType(SYS_HACKING), 0);
    bool preempt;
    {
        ScopedAIActionCallback callbackGuard;
        preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_HACK_PULSE_PRE, 2, 0);
    }

    if (!preempt) super();

    lua_pushboolean(context->GetLua(), preempt);
    {
        ScopedAIActionCallback callbackGuard;
        context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::COMBAT_AI_HACK_PULSE_POST, 3, 0);
    }
    lua_pop(context->GetLua(), 3);
}

HOOK_METHOD_PRIORITY(ShipAI, GetTeleportCommand, -10000, () -> std::pair<int, int>)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> ShipAI::GetTeleportCommand -> Begin (InternalEvents.cpp)\n")

    if (g_aiActionCallbackDepth > 0) return super();

    auto context = G_->getLuaContext();
    int command = TeleportCommand::NONE;
    int targetRoom = -1;
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipAI, 0);
    lua_pushinteger(context->GetLua(), command);
    lua_pushinteger(context->GetLua(), targetRoom);
    bool preempt;
    {
        ScopedAIActionCallback callbackGuard;
        preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SHIP_AI_TELEPORT_PRE, 3, 2);
    }
    if (lua_isnumber(context->GetLua(), -2)) command = static_cast<int>(lua_tointeger(context->GetLua(), -2));
    if (lua_isnumber(context->GetLua(), -1)) targetRoom = static_cast<int>(lua_tointeger(context->GetLua(), -1));
    lua_pop(context->GetLua(), 3);

    if (!preempt)
    {
        std::pair<int, int> result = super();
        command = result.first;
        targetRoom = result.second;
    }

    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipAI, 0);
    lua_pushboolean(context->GetLua(), preempt);
    lua_pushinteger(context->GetLua(), command);
    lua_pushinteger(context->GetLua(), targetRoom);
    {
        ScopedAIActionCallback callbackGuard;
        context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SHIP_AI_TELEPORT_POST, 4, 2);
    }
    if (lua_isnumber(context->GetLua(), -2)) command = static_cast<int>(lua_tointeger(context->GetLua(), -2));
    if (lua_isnumber(context->GetLua(), -1)) targetRoom = static_cast<int>(lua_tointeger(context->GetLua(), -1));
    lua_pop(context->GetLua(), 4);
    return {command, targetRoom};
}

static bool CallShipAIBoolPre(ShipAI *ai, InternalEvents::Identifiers event, bool &result)
{
    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), ai, context->getLibScript()->types.pShipAI, 0);
    lua_pushboolean(context->GetLua(), result);
    bool preempt;
    {
        ScopedAIActionCallback callbackGuard;
        preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(event, 2, 1);
    }
    if (lua_isboolean(context->GetLua(), -1)) result = lua_toboolean(context->GetLua(), -1);
    lua_pop(context->GetLua(), 2);
    return preempt;
}

static void CallShipAIBoolPost(ShipAI *ai, InternalEvents::Identifiers event, bool preempt, bool &result)
{
    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), ai, context->getLibScript()->types.pShipAI, 0);
    lua_pushboolean(context->GetLua(), preempt);
    lua_pushboolean(context->GetLua(), result);
    {
        ScopedAIActionCallback callbackGuard;
        context->getLibScript()->call_on_internal_chain_event_callbacks(event, 3, 1);
    }
    if (lua_isboolean(context->GetLua(), -1)) result = lua_toboolean(context->GetLua(), -1);
    lua_pop(context->GetLua(), 3);
}

HOOK_METHOD_PRIORITY(ShipAI, RequiredEvac, -10000, () -> bool)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> ShipAI::RequiredEvac -> Begin (InternalEvents.cpp)\n")
    if (g_aiActionCallbackDepth > 0) return super();
    bool result = false;
    bool preempt = CallShipAIBoolPre(this, InternalEvents::SHIP_AI_EVAC_PRE, result);
    if (!preempt) result = super();
    CallShipAIBoolPost(this, InternalEvents::SHIP_AI_EVAC_POST, preempt, result);
    return result;
}

HOOK_METHOD_PRIORITY(ShipAI, Surrender, -10000, () -> bool)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> ShipAI::Surrender -> Begin (InternalEvents.cpp)\n")
    if (g_aiActionCallbackDepth > 0) return super();
    bool result = false;
    bool preempt = CallShipAIBoolPre(this, InternalEvents::SHIP_AI_SURRENDER_PRE, result);
    if (!preempt) result = super();
    CallShipAIBoolPost(this, InternalEvents::SHIP_AI_SURRENDER_POST, preempt, result);
    return result;
}

HOOK_METHOD_PRIORITY(ShipAI, Escape, -10000, () -> bool)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> ShipAI::Escape -> Begin (InternalEvents.cpp)\n")
    if (g_aiActionCallbackDepth > 0) return super();
    bool result = false;
    bool preempt = CallShipAIBoolPre(this, InternalEvents::SHIP_AI_ESCAPE_PRE, result);
    if (!preempt) result = super();
    CallShipAIBoolPost(this, InternalEvents::SHIP_AI_ESCAPE_POST, preempt, result);
    return result;
}

static bool CallCrewAIPre(CrewAI *ai, InternalEvents::Identifiers event)
{
    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), ai, context->getLibScript()->types.pCrewAI, 0);
    bool preempt;
    {
        ScopedAIActionCallback callbackGuard;
        preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(event, 1, 0);
    }
    lua_pop(context->GetLua(), 1);
    return preempt;
}

static void CallCrewAIPost(CrewAI *ai, InternalEvents::Identifiers event, bool preempt)
{
    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), ai, context->getLibScript()->types.pCrewAI, 0);
    lua_pushboolean(context->GetLua(), preempt);
    {
        ScopedAIActionCallback callbackGuard;
        context->getLibScript()->call_on_internal_chain_event_callbacks(event, 2, 0);
    }
    lua_pop(context->GetLua(), 2);
}

HOOK_METHOD_PRIORITY(CrewAI, CheckForProblems, -10000, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CrewAI::CheckForProblems -> Begin (InternalEvents.cpp)\n")
    if (GetCurrentCrewAI() != this || g_aiActionCallbackDepth > 0) return super();
    bool preempt = CallCrewAIPre(this, InternalEvents::CREW_AI_PROBLEMS_PRE);
    if (!preempt) super();
    CallCrewAIPost(this, InternalEvents::CREW_AI_PROBLEMS_POST, preempt);
}

HOOK_METHOD_PRIORITY(CrewAI, UpdateIntruders, -10000, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CrewAI::UpdateIntruders -> Begin (InternalEvents.cpp)\n")
    if (GetCurrentCrewAI() != this || g_aiActionCallbackDepth > 0) return super();
    bool preempt = CallCrewAIPre(this, InternalEvents::CREW_AI_INTRUDERS_PRE);
    if (!preempt) super();
    CallCrewAIPost(this, InternalEvents::CREW_AI_INTRUDERS_POST, preempt);
}

HOOK_METHOD_PRIORITY(CrewAI, CheckForHealing, -10000, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CrewAI::CheckForHealing -> Begin (InternalEvents.cpp)\n")
    if (GetCurrentCrewAI() != this || g_aiActionCallbackDepth > 0) return super();
    bool preempt = CallCrewAIPre(this, InternalEvents::CREW_AI_HEALING_PRE);
    if (!preempt) super();
    CallCrewAIPost(this, InternalEvents::CREW_AI_HEALING_POST, preempt);
}

HOOK_METHOD_PRIORITY(CrewAI, UpdateDrones, -10000, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CrewAI::UpdateDrones -> Begin (InternalEvents.cpp)\n")
    if (GetCurrentCrewAI() != this || g_aiActionCallbackDepth > 0) return super();
    bool preempt = CallCrewAIPre(this, InternalEvents::CREW_AI_DRONES_PRE);
    if (!preempt) super();
    CallCrewAIPost(this, InternalEvents::CREW_AI_DRONES_POST, preempt);
}

HOOK_METHOD_PRIORITY(CrewAI, UpdateCrewMember, -10000, (int crewId) -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CrewAI::UpdateCrewMember -> Begin (InternalEvents.cpp)\n")
    if (GetCurrentCrewAI() != this || g_aiActionCallbackDepth > 0 || crewId < 0 || crewId >= crewList.size()) return super(crewId);

    CrewMember *crew = crewList[crewId];
    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pCrewAI, 0);
    SWIG_NewPointerObj(context->GetLua(), crew, context->getLibScript()->types.pCrewMember, 0);
    lua_pushinteger(context->GetLua(), crewId);
    bool preempt;
    {
        ScopedAIActionCallback callbackGuard;
        preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::CREW_AI_MEMBER_PRE, 3, 0);
    }

    if (!preempt) super(crewId);

    lua_pushboolean(context->GetLua(), preempt);
    {
        ScopedAIActionCallback callbackGuard;
        context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::CREW_AI_MEMBER_POST, 4, 0);
    }
    lua_pop(context->GetLua(), 4);
}

HOOK_METHOD_PRIORITY(CrewAI, CloseAirlocks, -10000, () -> bool)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CrewAI::CloseAirlocks -> Begin (InternalEvents.cpp)\n")
    if (GetCurrentCrewAI() != this || g_aiActionCallbackDepth > 0) return super();

    auto context = G_->getLuaContext();
    bool result = false;
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pCrewAI, 0);
    lua_pushboolean(context->GetLua(), result);
    bool preempt;
    {
        ScopedAIActionCallback callbackGuard;
        preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::CREW_AI_DOORS_PRE, 2, 1);
    }
    if (lua_isboolean(context->GetLua(), -1)) result = lua_toboolean(context->GetLua(), -1);
    lua_pop(context->GetLua(), 2);

    if (!preempt) result = super();

    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pCrewAI, 0);
    lua_pushboolean(context->GetLua(), preempt);
    lua_pushboolean(context->GetLua(), result);
    {
        ScopedAIActionCallback callbackGuard;
        context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::CREW_AI_DOORS_POST, 3, 1);
    }
    if (lua_isboolean(context->GetLua(), -1)) result = lua_toboolean(context->GetLua(), -1);
    lua_pop(context->GetLua(), 3);
    return result;
}

HOOK_METHOD_PRIORITY(CrewAI, SafeBlowoutOxygen, -10000, (int roomId) -> bool)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> CrewAI::SafeBlowoutOxygen -> Begin (InternalEvents.cpp)\n")
    if (GetCurrentCrewAI() != this || g_aiActionCallbackDepth > 0) return super(roomId);

    auto context = G_->getLuaContext();
    bool result = false;
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pCrewAI, 0);
    lua_pushinteger(context->GetLua(), roomId);
    lua_pushboolean(context->GetLua(), result);
    bool preempt;
    {
        ScopedAIActionCallback callbackGuard;
        preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::CREW_AI_AIRLOCK_PRE, 3, 1);
    }
    if (lua_isboolean(context->GetLua(), -1)) result = lua_toboolean(context->GetLua(), -1);
    lua_pop(context->GetLua(), 3);

    if (!preempt) result = super(roomId);

    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pCrewAI, 0);
    lua_pushinteger(context->GetLua(), roomId);
    lua_pushboolean(context->GetLua(), preempt);
    lua_pushboolean(context->GetLua(), result);
    {
        ScopedAIActionCallback callbackGuard;
        context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::CREW_AI_AIRLOCK_POST, 4, 1);
    }
    if (lua_isboolean(context->GetLua(), -1)) result = lua_toboolean(context->GetLua(), -1);
    lua_pop(context->GetLua(), 4);
    return result;
}

HOOK_METHOD_PRIORITY(ShipManager, JumpLeave, -10000, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> ShipManager::JumpLeave -> Begin (InternalEvents.cpp)\n")
    ShipAI *ai = GetCurrentShipAI();
    if (!ai || ai->ship != this || g_aiActionCallbackDepth > 0) return super();

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), ai, context->getLibScript()->types.pShipAI, 0);
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipManager, 0);
    bool preempt;
    {
        ScopedAIActionCallback callbackGuard;
        preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SHIP_AI_JUMP_PRE, 2, 0);
    }

    if (!preempt) super();

    lua_pushboolean(context->GetLua(), preempt);
    {
        ScopedAIActionCallback callbackGuard;
        context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SHIP_AI_JUMP_POST, 3, 0);
    }
    lua_pop(context->GetLua(), 3);
}

HOOK_METHOD_PRIORITY(WeaponControl, SelectArmament, -100, (unsigned int armamentSlot) -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> WeaponControl::SelectArmament -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    lua_pushinteger(context->GetLua(), armamentSlot);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SELECT_ARMAMENT_PRE, 1, 1);
    if (lua_isnumber(context->GetLua(), -1))
    {
        armamentSlot = static_cast<unsigned int>(lua_tonumber(context->GetLua(), -1));
        if (armamentSlot >= boxes.size() || armamentSlot < 0 || boxes[armamentSlot]->Empty()) preempt = true;
    }
    lua_pop(context->GetLua(), 1);

    if (!preempt) {
        super(armamentSlot);

        lua_pushinteger(context->GetLua(), armamentSlot);
        context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SELECT_ARMAMENT_POST, 1, 0);
        lua_pop(context->GetLua(), 1);
    }
}

//Priority to run after callback in CustomDrones.cpp
HOOK_METHOD_PRIORITY(SpaceDrone, GetNextProjectile, -100, () -> Projectile*)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> SpaceDrone::GetNextProjectile -> Begin (InternalEvents.cpp)\n")

    Projectile* ret = super();
    if (ret != nullptr)
    {
        auto context = G_->getLuaContext();
        SWIG_NewPointerObj(context->GetLua(), ret, context->getLibScript()->types.pProjectile[ret->GetType()], 0);
        SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pSpaceDroneTypes[this->type], 0);
        bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::DRONE_FIRE, 2, 0);
        lua_pop(context->GetLua(), 2);
        //preempt prevents projectile from firing
        if (preempt)
        {
            delete ret;
            return nullptr;
        }
    }
    return ret;
}

HOOK_METHOD(ShipManager, GetDodgeFactor, () -> int)
{
    LOG_HOOK("HOOK_METHOD -> ShipManager::GetDodgeFactor -> Begin (InternalEvents.cpp)\n")
    int ret = super();

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipManager, 0);
    lua_pushinteger(context->GetLua(), ret);
    context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::GET_DODGE_FACTOR, 2, 1);
    if (lua_isnumber(context->GetLua(), -1)) //Round floats and account for values like 1.0
    {
        ret = static_cast<int>(lua_tonumber(context->GetLua(), -1));
    }
    lua_pop(context->GetLua(), 2);
    return ret;
}

HOOK_METHOD(ShipSystem, SetBonusPower, (int amount, int permanentPower) -> void)
{
    LOG_HOOK("HOOK_METHOD -> ShipSystem::SetBonusPower -> Begin (InternalEvents.cpp)\n")

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipSystem, 0);
    lua_pushinteger(context->GetLua(), amount);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SET_BONUS_POWER, 2, 1);
    if (lua_isnumber(context->GetLua(), -1)) amount = static_cast<int>(lua_tonumber(context->GetLua(), -1));
    lua_pop(context->GetLua(), 2);

    if (!preempt) super(amount, permanentPower);
}
HOOK_METHOD(WeaponSystem, SetBonusPower, (int amount, int permanentPower) -> void)
{
    LOG_HOOK("HOOK_METHOD -> WeaponSystem::SetBonusPower -> Begin (InternalEvents.cpp)\n")

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pWeaponSystem, 0);
    lua_pushinteger(context->GetLua(), amount);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SET_BONUS_POWER, 2, 1);
    if (lua_isnumber(context->GetLua(), -1)) amount = static_cast<int>(lua_tonumber(context->GetLua(), -1));
    lua_pop(context->GetLua(), 2);

    if (!preempt) super(amount, permanentPower);
}
HOOK_METHOD(DroneSystem, SetBonusPower, (int amount, int permanentPower) -> void)
{
    LOG_HOOK("HOOK_METHOD -> DroneSystem::SetBonusPower -> Begin (InternalEvents.cpp)\n")

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pDroneSystem, 0);
    lua_pushinteger(context->GetLua(), amount);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SET_BONUS_POWER, 2, 1);
    if (lua_isnumber(context->GetLua(), -1)) amount = static_cast<int>(lua_tonumber(context->GetLua(), -1));
    lua_pop(context->GetLua(), 2);

    if (!preempt) super(amount, permanentPower);
}

static bool inArtilleryLoop = false;
HOOK_METHOD_PRIORITY(ArtillerySystem, OnLoop, -10000, () -> void)
{
    LOG_HOOK("HOOK_METHOD_PRIORITY -> ArtillerySystem::OnLoop -> Begin (InternalEvents.cpp)\n")

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->GetShipSystemType(SYS_ARTILLERY), 0);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::ARTILLERY_AI_PRE, 1, 0);

    if (!preempt)
    {
        ScopedAIContext aiContext(AIPhase::ARTILLERY, nullptr, nullptr, nullptr, this);
        inArtilleryLoop = true;
        super();
        inArtilleryLoop = false;
    }

    lua_pushboolean(context->GetLua(), preempt);
    context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::ARTILLERY_AI_POST, 2, 0);
    lua_pop(context->GetLua(), 2);
}
HOOK_METHOD(ProjectileFactory, SetCooldownModifier, (float mod) -> void)
{
    LOG_HOOK("HOOK_METHOD -> ProjectileFactory::SetCooldownModifier -> Begin (InternalEvents.cpp)\n")

    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pProjectileFactory, 0);
    lua_pushnumber(context->GetLua(), mod);
    lua_pushboolean(context->GetLua(), inArtilleryLoop);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::WEAPON_COOLDOWN_MOD, 3, 1);
    if (lua_isnumber(context->GetLua(), -1))
    {
        mod = std::max(0.f, static_cast<float>(lua_tonumber(context->GetLua(), -1)));
        if (!inArtilleryLoop)
        {
            mod = std::min(mod, 1.f);
        }
    }
    lua_pop(context->GetLua(), 3);

    if (!preempt) super(mod);
}

HOOK_METHOD(ShipManager, JumpArrive, () -> void)
{
    LOG_HOOK("HOOK_METHOD -> ShipManager::JumpArrive -> Begin (InternalEvents.cpp)\n")
    super();
    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipManager, 0);
    context->getLibScript()->call_on_internal_event_callbacks(InternalEvents::JUMP_ARRIVE, 1);
    lua_pop(context->GetLua(), 1);
}

HOOK_METHOD(ShipManager, JumpLeave, () -> void)
{
    LOG_HOOK("HOOK_METHOD -> ShipManager::JumpLeave -> Begin (InternalEvents.cpp)\n")
    super();
    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipManager, 0);
    context->getLibScript()->call_on_internal_event_callbacks(InternalEvents::JUMP_LEAVE, 1);
    lua_pop(context->GetLua(), 1);
}
//To be used for button MouseMove functions as to create proper beep sounds and mouse pointer animation changes.
HOOK_METHOD(CommandGui, MouseMove, (int mX, int mY) -> void)
{
    LOG_HOOK("HOOK_METHOD -> CommandGui::MouseMove -> Begin (InternalEvents.cpp)\n")
    auto context = Global::GetInstance()->getLuaContext();
    lua_pushinteger(context->GetLua(), mX);
    lua_pushinteger(context->GetLua(), mY);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::GUI_MOUSE_MOVE, 2, 0);
    lua_pop(context->GetLua(), 2);

    if (!preempt) super(mX, mY);
}

HOOK_METHOD(ShipManager, Wait, () -> void)
{
    LOG_HOOK("HOOK_METHOD -> ShipManager::Wait -> Begin (InternalEvents.cpp)\n")
    super();
    auto context = G_->getLuaContext();
    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pShipManager, 0);
    context->getLibScript()->call_on_internal_event_callbacks(InternalEvents::ON_WAIT, 1);
    lua_pop(context->GetLua(), 1);
}

HOOK_METHOD(SystemBox, MouseMove, (int x, int y) -> void)
{
    LOG_HOOK("HOOK_METHOD -> SystemBox::MouseMove -> Begin (InternalEvents.cpp)\n")
    auto context = Global::GetInstance()->getLuaContext();

    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pSystemBox, 0);
    //Coordinates are relative to the SystemBox
    lua_pushinteger(context->GetLua(), x - location.x);
    lua_pushinteger(context->GetLua(), y - location.y);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SYSTEM_BOX_MOUSE_MOVE, 3, 0);

    lua_pop(context->GetLua(), 3);

    if (!preempt) super(x, y);
}
//NOTE: Return seems to indicate if the click was successful, so it will be false if preempted. If this needs to be true in some preempt cases allow user to optionally provide their own return value.
HOOK_METHOD(SystemBox, MouseClick, (bool shift) -> bool)
{
    LOG_HOOK("HOOK_METHOD -> SystemBox::MouseClick -> Begin (InternalEvents.cpp)\n")
    auto context = Global::GetInstance()->getLuaContext();

    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pSystemBox, 0);
    lua_pushboolean(context->GetLua(), shift);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SYSTEM_BOX_MOUSE_CLICK, 2, 0);

    lua_pop(context->GetLua(), 2);
    if (!preempt) return super(shift);
    else return false;
}
HOOK_METHOD(SystemBox, KeyDown, (SDLKey key, bool shift) -> void)
{
    LOG_HOOK("HOOK_METHOD -> SystemBox::KeyDown -> Begin (InternalEvents.cpp)\n")
    auto context = Global::GetInstance()->getLuaContext();

    SWIG_NewPointerObj(context->GetLua(), this, context->getLibScript()->types.pSystemBox, 0);
    lua_pushinteger(context->GetLua(), key);
    lua_pushboolean(context->GetLua(), shift);
    bool preempt = context->getLibScript()->call_on_internal_chain_event_callbacks(InternalEvents::SYSTEM_BOX_KEY_DOWN, 3, 0);

    lua_pop(context->GetLua(), 3);
    if (!preempt) super(key, shift);
}
//Might make more sense for this to be structured to have one function per custom system id but we'll use regular callbacks for now
HOOK_STATIC(ShipSystem, GetLevelDescription, (int systemId, int level, bool tooltip) -> std::string)
{
    LOG_HOOK("HOOK_STATIC -> ShipSystem::GetLevelDescription -> Begin (InternalEvents.cpp)\n")

    auto context = Global::GetInstance()->getLuaContext();

    lua_pushinteger(context->GetLua(), systemId);
    lua_pushinteger(context->GetLua(), level + 1); //Push true level
    lua_pushboolean(context->GetLua(), tooltip);
    if (context->getLibScript()->call_on_internal_event_callbacks(InternalEvents::GET_LEVEL_DESCRIPTION, 3, 1) == 1 && lua_isstring(context->GetLua(), -1))
    {
        std::string ret = lua_tostring(context->GetLua(), -1);
        lua_pop(context->GetLua(), 4);
        return ret;
    }
    else // No return from callback
    {
        lua_pop(context->GetLua(), 3);
        return super(systemId, level, tooltip);
    }
}
