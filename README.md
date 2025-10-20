
Autobalance Modul für die Azerothcore-Playerbot fork von @liyunfan1223 unter https://github.com/liyunfan1223/azerothcore-wotlk.git



# mod-fortis-autobalance

Skaliert HP und Schaden von Kreaturen in Instanzen/Raids linear nach anwesender Spielerzahl:

Unter Baseline: Mobs sind schwächer (z. B. 1 Spieler in 5er-Instanz ≈ 20 %).

Bei Baseline: Mobs sind 100 % (Standard).

Über Baseline: Optional >100 % aktivierbar.

Keine Chat-Kommandos. Keine DB-Schreibzugriffe. Reine Laufzeit-Anpassung bei Kampfbeginn, Rücksetzung bei Evade/Tod.

Funktionsweise

Spielerzählung: GMs werden nicht gezählt, solange .gm on; bei .gm off werden sie gezählt.

Playerbots werden gezählt (Player-Objekte).

Linearer Faktor (Standard):

Multiplier = clamp( players / BaselinePlayers, MinMultiplier, 1.0 )

Mit AllowAboveBase = 1: Multiplier = max(players / BaselinePlayers, MinMultiplier) (ohne Obergrenze).


Beispiel (BaselinePlayers = 5):


Spieler	Multiplier	Wirkung

1	0,20	20 % HP/DMG

2	0,40	40 %

3	0,60	60 %

4	0,80	80 %

5	1,00	100 %

6	1,20 (nur wenn AllowAboveBase=1)	120 %


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
