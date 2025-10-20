#include "ScriptMgr.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "Unit.h"
#include "Creature.h"
#include "Player.h"

#include <unordered_map>
#include <algorithm>

namespace FortisAB
{
    struct Settings
    {
        bool   Enable           = true;
        bool   InstanceOnly     = true;   // nur Dungeon/Raid
        uint32 BaselinePlayers  = 5;      // z. B. 5 für 5er-Instanz
        float  MinMultiplier    = 0.0f;   // 0.0 = automatisch 1/Baseline
        bool   AllowAboveBase   = false;  // >1.0 erlauben? (Standard: nein)
    };

    static Settings s;

    struct SavedStats
    {
        uint32 maxHealth;
        float  baseMin[2];
        float  baseMax[2];
        bool   applied = false;
    };

    static std::unordered_map<Creature*, SavedStats> g_saved;

    static inline bool IsInstanceMap(const Map* m)
    {
        return m && (m->IsDungeon() || m->IsRaid());
    }

    static inline uint32 CountRelevantPlayers(const Map* map)
    {
        if (!map) return 0;
        uint32 n = 0;
        for (auto const& ref : map->GetPlayers())
        {
            if (Player* p = ref.GetSource())
            {
                // GMs zählen nicht (nur bei .gm off würden sie gezählt)
                if (!p->IsGameMaster())
                    ++n;
            }
        }
        return n;
    }

    // Linear: 1 Spieler -> ~1/Baseline, 5 Spieler (Baseline=5) -> 1.0
    static inline float ComputeMultiplier(uint32 playerCount)
    {
        uint32 base = s.BaselinePlayers ? s.BaselinePlayers : 1;
        float ratio = float(std::max<uint32>(playerCount, 1)) / float(base);

        float minMul = s.MinMultiplier > 0.0f ? s.MinMultiplier : (1.0f / float(base));
        if (!s.AllowAboveBase && ratio > 1.0f)
            ratio = 1.0f;
        if (ratio < minMul)
            ratio = minMul;
        return ratio; // 0.2 .. 1.0 bei Baseline=5
    }

    static void ApplyScaling(Creature* c, float mult)
    {
        if (!c || mult == 1.0f)
            return;

        auto& slot = g_saved[c];
        if (slot.applied)
            return; // bereits skaliert

        // Originalwerte sichern
        slot.maxHealth  = c->GetMaxHealth();
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
            if (minD > 0.f && maxD > 0.f)
            {
                c->SetBaseWeaponDamage(WeaponAttackType(i), MINDAMAGE, minD * mult);
                c->SetBaseWeaponDamage(WeaponAttackType(i), MAXDAMAGE, maxD * mult);
                c->UpdateDamagePhysical(WeaponAttackType(i));
            }
        }

        slot.applied = true;

        // INFO, damit du es im Journal siehst
        LOG_INFO("module", "mod-fortis-autobalance: applied x%.2f to creature %s (map %u, players=%u)",
                 mult, c->GetGUID().ToString().c_str(), c->GetMapId(),
                 c->GetMap() ? FortisAB::CountRelevantPlayers(c->GetMap()) : 0);
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

        LOG_INFO("module", "mod-fortis-autobalance: reverted for creature %s",
                 c->GetGUID().ToString().c_str());
    }
}

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

        FortisAB::s.Enable          = getb ("FortisAB.Enable",          true);
        FortisAB::s.InstanceOnly    = getb ("FortisAB.InstanceOnly",    true);
        FortisAB::s.BaselinePlayers = uint32(geti("FortisAB.BaselinePlayers", 5));
        FortisAB::s.MinMultiplier   = getf ("FortisAB.MinMultiplier",   0.0f);  // 0.0 = auto 1/Baseline
        FortisAB::s.AllowAboveBase  = getb ("FortisAB.AllowAboveBase",  false);

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
