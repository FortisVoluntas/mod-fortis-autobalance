#include "ScriptMgr.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "Unit.h"
#include "Creature.h"
#include "Player.h"
#include "Group.h"

#include <unordered_map>

namespace FortisAB
{
    struct Settings
    {
        bool   Enable                = true;
        bool   InstanceOnly          = true;     // nur Dungeon/Raid
        bool   CountPlayerbots       = true;     // Bots als Spieler zählen (Player-Objekte)
        uint32 BaselinePlayers       = 5;        // Referenz
        float  HealthPerExtraPlayer  = 0.50f;    // +50% HP je zusätzl. Spieler
        float  DamagePerExtraPlayer  = 0.30f;    // +30% Grundschaden je zusätzl. Spieler
        float  MaxMultiplier         = 3.0f;     // Deckel
    };

    static Settings s;

    struct SavedStats
    {
        uint32 maxHealth;
        float  baseMin[2];
        float  baseMax[2];
        bool   applied = false;
    };

    // Laufzeit-Map: pro Creature Originalwerte
    static std::unordered_map<Creature*, SavedStats> g_saved;

    static inline bool IsInstanceMap(const Map* m)
    {
        return m && (m->IsDungeon() || m->IsRaid());
    }

    static inline uint32 CountRelevantPlayers(const Map* map)
    {
        if (!map) return 0;
        uint32 n = 0;
        Map::PlayerList const& pl = map->GetPlayers();
        for (auto const& ref : pl)
        {
            if (Player* p = ref.GetSource())
            {
                // GMs nicht zählen (nur echte Spieler). Bei ".gm off" werden sie gezählt.
                if (!p->IsGameMaster())
                    ++n;
            }
        }
        return n;
    }

    static inline float ComputeMultiplier(uint32 playerCount)
    {
        if (playerCount <= s.BaselinePlayers)
            return 1.0f;

        const uint32 extra = playerCount - s.BaselinePlayers;
        float m = 1.0f + s.HealthPerExtraPlayer * extra;
        if (m > s.MaxMultiplier)
            m = s.MaxMultiplier;
        return m;
    }

    static void ApplyScaling(Creature* c, float mult)
    {
        if (!c || mult <= 1.0f)
            return;

        auto& slot = g_saved[c];
        if (slot.applied)
            return; // bereits skaliert

        // Originalwerte sichern
        slot.maxHealth = c->GetMaxHealth();
        slot.baseMin[0] = c->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE);
        slot.baseMax[0] = c->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE);
        slot.baseMin[1] = c->GetWeaponDamageRange(OFF_ATTACK,  MINDAMAGE);
        slot.baseMax[1] = c->GetWeaponDamageRange(OFF_ATTACK,  MAXDAMAGE);

        // HP skalieren
        const uint32 newMax = uint32(float(slot.maxHealth) * mult);
        c->SetMaxHealth(newMax);
        uint32 newCur = uint32(float(c->GetHealth()) * mult);
        if (newCur > newMax) newCur = newMax;
        c->SetHealth(newCur);

        // Grundschaden skalieren
        for (uint8 i = BASE_ATTACK; i <= OFF_ATTACK; ++i)
        {
            float minD = c->GetWeaponDamageRange(WeaponAttackType(i), MINDAMAGE);
            float maxD = c->GetWeaponDamageRange(WeaponAttackType(i), MAXDAMAGE);
            if (minD > 0.0f && maxD > 0.0f)
            {
                c->SetBaseWeaponDamage(WeaponAttackType(i), MINDAMAGE, minD * mult);
                c->SetBaseWeaponDamage(WeaponAttackType(i), MAXDAMAGE, maxD * mult);
                c->UpdateDamagePhysical(WeaponAttackType(i));
            }
        }

        slot.applied = true;

        LOG_DEBUG("module", "mod-fortis-autobalance: applied x%.2f to creature %s (map %u)",
                  mult, c->GetGUID().ToString().c_str(), c->GetMapId());
    }

    static void RevertScaling(Creature* c)
    {
        if (!c) return;
        auto it = g_saved.find(c);
        if (it == g_saved.end() || !it->second.applied)
            return;

        SavedStats const& st = it->second;

        c->SetMaxHealth(st.maxHealth);
        if (c->GetHealth() > st.maxHealth)
            c->SetHealth(st.maxHealth);

        c->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, st.baseMin[0]);
        c->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, st.baseMax[0]);
        c->SetBaseWeaponDamage(OFF_ATTACK,  MINDAMAGE, st.baseMin[1]);
        c->SetBaseWeaponDamage(OFF_ATTACK,  MAXDAMAGE, st.baseMax[1]);
        c->UpdateDamagePhysical(BASE_ATTACK);
        c->UpdateDamagePhysical(OFF_ATTACK);

        g_saved.erase(it);

        LOG_DEBUG("module", "mod-fortis-autobalance: reverted for creature %s",
                  c->GetGUID().ToString().c_str());
    }
}

// Automatik über UnitScript-Hooks
class FortisAutobalance_Unit : public UnitScript
{
public:
    FortisAutobalance_Unit() : UnitScript("FortisAutobalance_Unit") { }

    void OnUnitEnterCombat(Unit* unit, Unit* /*victim*/) override
    {
        if (!unit || unit->GetTypeId() != TYPEID_UNIT)
            return;
        if (!FortisAB::s.Enable)
            return;

        Creature* c = unit->ToCreature();
        if (!c) return;

        Map* map = c->GetMap();
        if (FortisAB::s.InstanceOnly && !FortisAB::IsInstanceMap(map))
            return;

        const uint32 n = FortisAB::CountRelevantPlayers(map);
        const float  m = FortisAB::ComputeMultiplier(n);
        if (m <= 1.0f) return;

        FortisAB::ApplyScaling(c, m);
    }

    void OnUnitEnterEvadeMode(Unit* unit, uint8 /*why*/) override
    {
        if (unit && unit->GetTypeId() == TYPEID_UNIT)
            FortisAB::RevertScaling(unit->ToCreature());
    }

    void OnUnitDeath(Unit* unit, Unit* /*killer*/) override
    {
        if (unit && unit->GetTypeId() == TYPEID_UNIT)
            FortisAB::RevertScaling(unit->ToCreature());
    }
};

class FortisAutobalance_World final : public WorldScript
{
public:
    FortisAutobalance_World() : WorldScript("FortisAutobalance_World") { }

    void OnAfterConfigLoad(bool reload) override
    {
        auto getf = [](char const* k, float def){ return sConfigMgr->GetOption<float>(k, def); };
        auto geti = [](char const* k, int   def){ return sConfigMgr->GetOption<int>(k, def);   };
        auto getb = [](char const* k, bool  def){ return sConfigMgr->GetOption<bool>(k, def);  };

        FortisAB::s.Enable               = getb ("FortisAB.Enable",               true);
        FortisAB::s.InstanceOnly         = getb ("FortisAB.InstanceOnly",         true);
        FortisAB::s.CountPlayerbots      = getb ("FortisAB.CountPlayerbots",      true);
        FortisAB::s.BaselinePlayers      = uint32(geti("FortisAB.BaselinePlayers",5));
        FortisAB::s.HealthPerExtraPlayer = getf ("FortisAB.HealthPerExtraPlayer", 0.50f);
        FortisAB::s.DamagePerExtraPlayer = getf ("FortisAB.DamagePerExtraPlayer", 0.30f);
        FortisAB::s.MaxMultiplier        = getf ("FortisAB.MaxMultiplier",        3.0f);

        LOG_INFO("module", "mod-fortis-autobalance: config loaded (reload=%d)", int(reload));
    }

    void OnStartup() override
    {
        new FortisAutobalance_Unit();
        LOG_INFO("module", "mod-fortis-autobalance: loaded");
    }
};

void AddSC_FortisAutobalance()
{
    new FortisAutobalance_World();
}
