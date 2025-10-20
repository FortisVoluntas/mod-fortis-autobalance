
Autobalance Modul für die Azerothcore-Playerbot fork von @liyunfan1223 unter https://github.com/liyunfan1223/azerothcore-wotlk.git



# mod-fortis-autobalance

Skaliert **HP** und **Grundschaden** von Kreaturen in **Instanzen/Raids** automatisch anhand der anwesenden Spielerzahl.  
Keinerlei Chat-Kommandos, keine DB-Schreibzugriffe – reiner Runtime-Scale bei Kampfbeginn, Rücksetzung bei Evade/Tod.

## Features
- Automatische Skalierung bei **Kampfbeginn**
- **Revert** bei **Evade** oder **Tod**
- **GMs** werden **nicht** gezählt, solange `.gm on`; bei `.gm off` werden sie als normale Spieler gezählt
- **Playerbots** werden gezählt (sind `Player`-Objekte)
- Konfigurierbar über `FortisAB.*`

## Formel (vereinfacht)
- `Multiplier = min( 1.0 + HealthPerExtraPlayer * max(0, Players - BaselinePlayers), MaxMultiplier )`
- HP und Basis-Waffenschaden (BaseAttack/Offhand) werden mit `Multiplier` hochskaliert.

## Installation
1. Dieses Repo in `azerothcore/modules/mod-fortis-autobalance` klonen oder kopieren.
2. **Build**:
   ```bash
   cd /root/azerothcore/build
   cmake ..
   make -j"$(nproc)" worldserver
   cd /root/azerothcore/build
   make install
3. install -m 644 /root/azerothcore/modules/mod-fortis-autobalance/conf/mod_fortis_autobalance.conf.dist \
  /root/azerothcore/env/dist/etc/modules/mod_fortis_autobalance.conf.dist
4. cp /root/azerothcore/env/dist/etc/modules/mod_fortis_autobalance.conf.dist \
   /root/azerothcore/env/dist/etc/modules/mod_fortis_autobalance.conf
