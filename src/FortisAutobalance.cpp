#include "ScriptMgr.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "Unit.h"
#include "Creature.h"
#include "Player.h"
#include "SpellInfo.h"

#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace FortisAB
{
    struct Settings
    {
        bool   Enable           = true;
        bool   InstanceOnly     = true;   // nur Dungeon/Raid
        uint32 BaselinePlayers  = 5;      // z. B. 5 für 5er-Instanz
        float  MinMultiplier    = 0.0f;   // 0.0 = automatisch 1/Baseline
        bool   AllowAboveBase   = false;  // >1.0 erlauben?
    };

    static Settings s;

    struct SavedStats
    {
        uint32 maxHealth = 0;
        bool   applied   = false;
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
                if (!p->IsGameMaster())
                    ++n;
            }
        }
        return n;
    }

    // Linear: players / baseline, mit Untergrenze und optionaler Obergrenze 1.0
    static inline float ComputeLinearMultiplier(uint32 playerCount)
    {
        uint32 base = s.BaselinePlayers ? s.BaselinePlayers : 1;
        float  ratio = float(std::max<uint32>(playerCount, 1)) / float(base);

        float minMul = s.MinMultiplier > 0.0f ? s.MinMultiplier : (1.0f / float(base));
        if (!s.AllowAboveBase && ratio > 1.0f)
            ratio = 1.0f;
        if (ratio < minMul)
            ratio = minMul;
        return ratio;
    }

    // Nur HP skalieren (Schaden wird in Hooks skaliert)
    static void ApplyHpScaling(Creature* c, float mult)
    {
        if (!c || mult == 1.0f)
            return;

        auto& slot = g_saved[c];
        if (slot.applied)
            return;

        slot.maxHealth = c->GetMaxHealth();

        const uint32 newMax = uint32(float(slot.maxHealth) * mult);
        c->SetMaxHealth(newMax);
        uint32 newCur = uint32(float(c->GetHealth()) * mult);
        if (newCur > newMax) newCur = newMax;
        c->SetHealth(newCur);

        slot.applied = true;

        LOG_INFO("module", "mod-fortis-autobalance: applied HP x%.2f to creature %s (map %u)",
                 mult, c->GetGUID().ToString().c_str(), c->GetMapId());
    }

    static void RevertHpScaling(Creature* c)
    {
        if (!c) return;
        auto it = g_saved.find(c);
        if (it == g_saved.end() || !it->second.applied)
            return;

        SavedStats const& st = it->second;

        c->SetMaxHealth(st.maxHealth);
        if (c->GetHealth() > st.maxHealth)
            c->SetHealth(st.maxHealth);

        g_saved.erase(it);

        LOG_INFO("module", "mod-fortis-autobalance: reverted HP for creature %s",
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
        const float  m = FortisAB::ComputeLinearMultiplier(n);

        FortisAB::ApplyHpScaling(c, m);
    }

    void OnUnitEnterEvadeMode(Unit* unit, uint8 /*why*/) override
    {
        if (unit && unit->GetTypeId() == TYPEID_UNIT)
            FortisAB::RevertHpScaling(unit->ToCreature());
    }

    void OnUnitDeath(Unit* unit, Unit* /*killer*/) override
    {
        if (unit && unit->GetTypeId() == TYPEID_UNIT)
            FortisAB::RevertHpScaling(unit->ToCreature());
    }
};

// Skaliert JEGLICHEN ausgehenden Schaden der Kreatur (Melee, Spell, Periodic)
class FortisAutobalance_Damage : public UnitScript
{
public:
    FortisAutobalance_Damage() : UnitScript("FortisAutobalance_Damage") { }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        if (!attacker || attacker->GetTypeId() != TYPEID_UNIT || damage == 0)
            return;
        if (!FortisAB::s.Enable)
            return;

        Map* map = attacker->GetMap();
        if (FortisAB::s.InstanceOnly && !FortisAB::IsInstanceMap(map))
            return;

        float m = FortisAB::ComputeLinearMultiplier(FortisAB::CountRelevantPlayers(map));
        damage = uint32(std::lround(damage * m));
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (!attacker || attacker->GetTypeId() != TYPEID_UNIT || damage <= 0)
            return;
        if (!FortisAB::s.Enable)
            return;

        Map* map = attacker->GetMap();
        if (FortisAB::s.InstanceOnly && !FortisAB::IsInstanceMap(map))
            return;

        float m = FortisAB::ComputeLinearMultiplier(FortisAB::CountRelevantPlayers(map));
        damage = int32(std::lround(float(damage) * m));
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (!attacker || attacker->GetTypeId() != TYPEID_UNIT || damage == 0)
            return;
        if (!FortisAB::s.Enable)
            return;

        Map* map = attacker->GetMap();
        if (FortisAB::s.InstanceOnly && !FortisAB::IsInstanceMap(map))
            return;

        float m = FortisAB::ComputeLinearMultiplier(FortisAB::CountRelevantPlayers(map));
        damage = uint32(std::lround(float(damage) * m));
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
        new FortisAutobalance_Damage();
        LOG_INFO("module", "mod-fortis-autobalance: loaded");
    }
};

void AddSC_FortisAutobalance()
{
    new FortisAutobalance_World();
}
