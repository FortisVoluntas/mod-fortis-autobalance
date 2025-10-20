#include "ScriptMgr.h"
#include "Config.h"
#include "Log.h"
#include "Map.h"
#include "Unit.h"
#include "Creature.h"
#include "Player.h"
#include "SpellInfo.h"

#include <algorithm>
#include <cmath>

namespace FortisAB
{
    struct Settings
    {
        bool   Enable           = true;
        bool   InstanceOnly     = true;   // nur Dungeon/Raid
        uint32 BaselinePlayers  = 5;      // 100% bei Baseline
        float  MinMultiplier    = 0.0f;   // 0.0 => auto 1/Baseline
        bool   AllowAboveBase   = false;  // >100% oberhalb Baseline erlauben?
    };
    static Settings s;

    static inline bool IsInstanceMap(const Map* m)
    {
        return m && (m->IsDungeon() || m->IsRaid());
    }

    static inline uint32 CountRelevantPlayers(const Map* map)
    {
        if (!map) return 0;
        uint32 n = 0;
        for (auto const& ref : map->GetPlayers())
            if (Player* p = ref.GetSource())
                if (!p->IsGameMaster()) // GMs nur bei .gm off gezählt
                    ++n;
        return n;
    }

    // Verhältnis players/baseline, mit Untergrenze und optionalem Deckel bei 1.0
    static inline float RatioPlayersToBaseline(uint32 players)
    {
        uint32 base = s.BaselinePlayers ? s.BaselinePlayers : 1;
        float ratio = float(std::max<uint32>(players, 1)) / float(base);

        float minMul = s.MinMultiplier > 0.0f ? s.MinMultiplier : (1.0f / float(base));
        if (!s.AllowAboveBase && ratio > 1.0f)
            ratio = 1.0f;
        if (ratio < minMul)
            ratio = minMul;
        return ratio; // z. B. 0.20 .. 1.0 bei Baseline=5
    }

    // Multiplikator für Schaden VOM Mob AUF Spieler (bei Solo kleiner, z. B. 0.20)
    static inline float CreatureOutgoingMul(Map* map)
    {
        return RatioPlayersToBaseline(CountRelevantPlayers(map));
    }

    // Multiplikator für Schaden VOM Spieler AUF Mob (bei Solo größer, z. B. 5.0)
    static inline float PlayerOutgoingMul(Map* map)
    {
        float r = RatioPlayersToBaseline(CountRelevantPlayers(map));
        return r > 0.0f ? (1.0f / r) : 1.0f;
    }

    static inline bool InScopeForBalance(Unit* a, Unit* b)
    {
        if (!a || !b) return false;
        Map* m = a->GetMap();
        if (!m || m != b->GetMap()) return false;
        if (s.InstanceOnly && !IsInstanceMap(m)) return false;
        return s.Enable;
    }
}

// Skaliert JEDEN Schaden kontextabhängig (Melee, Zauber, Periodic)
class FortisAutobalance_Damage : public UnitScript
{
public:
    FortisAutobalance_Damage() : UnitScript("FortisAutobalance_Damage") { }

    // Physische Treffer (Melee/Ranged)
    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        if (!damage) return;
        if (!FortisAB::InScopeForBalance(attacker, target)) return;

        Map* map = attacker->GetMap();

        // Mob -> Spieler: Mob-Schaden reduzieren (oder erhöhen über Baseline, falls erlaubt)
        if (attacker->GetTypeId() == TYPEID_UNIT && target->GetTypeId() == TYPEID_PLAYER)
        {
            float m = FortisAB::CreatureOutgoingMul(map);
            damage = uint32(std::lround(damage * m));
            return;
        }

        // Spieler (oder dessen Pet/Vehicle) -> Mob: Spielerschaden multiplizieren
        if (target->GetTypeId() == TYPEID_UNIT)
        {
            if (Player* owner = attacker->GetCharmerOrOwnerPlayerOrPlayerItself())
            {
                (void)owner; // nur Präsenzprüfung
                float m = FortisAB::PlayerOutgoingMul(map);
                damage = uint32(std::lround(damage * m));
            }
        }
    }

    // Direkter Zauberschaden
    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (damage <= 0) return;
        if (!FortisAB::InScopeForBalance(attacker, target)) return;

        Map* map = attacker->GetMap();

        if (attacker->GetTypeId() == TYPEID_UNIT && target->GetTypeId() == TYPEID_PLAYER)
        {
            float m = FortisAB::CreatureOutgoingMul(map);
            damage = int32(std::lround(float(damage) * m));
            return;
        }

        if (target->GetTypeId() == TYPEID_UNIT)
        {
            if (Player* owner = attacker->GetCharmerOrOwnerPlayerOrPlayerItself())
            {
                (void)owner;
                float m = FortisAB::PlayerOutgoingMul(map);
                damage = int32(std::lround(float(damage) * m));
            }
        }
    }

    // Periodische Ticks (DoTs, auras)
    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (!damage) return;
        if (!FortisAB::InScopeForBalance(attacker, target)) return;

        Map* map = attacker->GetMap();

        if (attacker->GetTypeId() == TYPEID_UNIT && target->GetTypeId() == TYPEID_PLAYER)
        {
            float m = FortisAB::CreatureOutgoingMul(map);
            damage = uint32(std::lround(float(damage) * m));
            return;
        }

        if (target->GetTypeId() == TYPEID_UNIT)
        {
            if (Player* owner = attacker->GetCharmerOrOwnerPlayerOrPlayerItself())
            {
                (void)owner;
                float m = FortisAB::PlayerOutgoingMul(map);
                damage = uint32(std::lround(float(damage) * m));
            }
        }
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
        FortisAB::s.MinMultiplier   = getf ("FortisAB.MinMultiplier",   0.0f);  // 0.0 => 1/Baseline
        FortisAB::s.AllowAboveBase  = getb ("FortisAB.AllowAboveBase",  false);

        LOG_INFO("module", "mod-fortis-autobalance: config loaded (reload=%d)", int(reload));
    }

    void OnStartup() override
    {
        new FortisAutobalance_Damage();
        LOG_INFO("module", "mod-fortis-autobalance: loaded");
    }
};

void AddSC_FortisAutobalance()
{
    new FortisAutobalance_World();
}
