# mod-fortis-autobalance

**Why you’ll want this:
Turn empty dungeons into playable, rewarding content—right now. On low-pop servers, the module lets you solo dungeons by smartly scaling damage in your favor; as friends (or bots) join, the modifiers fade to 100% normal for a true group experience. No gimmicks, no grindy tweaks—just smooth difficulty that adapts to your party size.
Jump in, pull a pack, and feel the difference.**


**Auto-balance module** for the **AzerothCore Playerbot fork** by [@liyunfan1223](https://github.com/liyunfan1223/azerothcore-wotlk.git)

---

## Purpose

- **No HP manipulation.** Creatures keep their full health.
- **Player/Pet → Creature:** outgoing player damage is **multiplied** based on party size.
- **Creature → Player:** outgoing creature damage is **reduced** (optionally increased above baseline).
- **Loot remains correct**, because all effective damage comes from players/pets.

---

## How it works

- Scope: **Dungeons/Raids** when `InstanceOnly=1`.
- Counting: all **non-GM** players on the map (a GM is counted when `.gm off`). **Playerbots** are counted.
- Covered: **melee/ranged**, **spell**, **periodic (DoT) ticks**.
- **Not reduced by default:** creature → **pets** (can be enabled on request).

### Factors

Let `p = max(counted_players, 1)`, `b = BaselinePlayers`, `r = p / b`,  
with lower bound `MinMultiplier` (default: `1/b`) and—if `AllowAboveBase=0`—upper bound `1.0`.

- **Creature → Player:**  
  `new_damage = old_damage * clamp(r, MinMultiplier, (AllowAboveBase ? ∞ : 1.0))`
- **Player/Pet → Creature:**  
  `new_damage = old_damage * (1 / clamp(r, MinMultiplier, (AllowAboveBase ? ∞ : 1.0)))`

### Example (BaselinePlayers = 5, AllowAboveBase = 0)

| Players | r = p/5 | Creature → Player | Player → Creature |
|-------:|:--------:|:-----------------:|:-----------------:|
| 1      | 0.20     | ×0.20             | ×5.00             |
| 3      | 0.60     | ×0.60             | ×1.67             |
| 5      | 1.00     | ×1.00             | ×1.00             |

> With `AllowAboveBase = 1`, factors for groups larger than the baseline can exceed 1.0.

---

## Installation

1. Place or clone this module into `azerothcore/modules/mod-fortis-autobalance`.
2. Build & install:
   ```bash
   cd /root/azerothcore/build
   cmake ..
   make -j"$(nproc)" worldserver
   make install
3. Provide the config if not auto-installed:
   ```bash
   install -m 644 /root/azerothcore/modules/mod-fortis-autobalance/conf/mod_fortis_autobalance.conf.dist \
     /root/azerothcore/env/dist/etc/modules/mod_fortis_autobalance.conf.dist
4. (Optional) Activate your own config:
   ```bash
   cp /root/azerothcore/env/dist/etc/modules/mod_fortis_autobalance.conf.dist \
      /root/azerothcore/env/dist/etc/modules/mod_fortis_autobalance.conf
5. Restart:
   ```bash
   systemctl restart azerothcore.service
