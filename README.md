# mod-fortis-autobalance

**Autobalance-Modul** für die **AzerothCore-Playerbot Fork** von [@liyunfan1223](https://github.com/liyunfan1223):  
https://github.com/liyunfan1223/azerothcore-wotlk.git

---

## Kurzbeschreibung

Skaliert **HP** und **Schaden** von Kreaturen in **Instanzen/Raids** **linear** nach anwesender Spielerzahl:

- **Unter Baseline:** Mobs sind schwächer (z. B. 1 Spieler in 5er-Instanz ≈ **20 %**).
- **Bei Baseline:** Mobs sind **100 %** (Standard).
- **Über Baseline:** Optional **> 100 %** aktivierbar.

Eigenschaften: keine Chat-Kommandos, keine DB-Schreibzugriffe, reine Laufzeit-Anpassung bei **Kampfbeginn**, Rücksetzung bei **Evade/Tod**.

(das Ganze funktioniert zu diesem Zeitpunkt schon, es gibt nur Probleme beim looten)
---

## Funktionsweise

- **Spielerzählung:** GMs werden **nicht** gezählt, solange `.gm on`; bei `.gm off` werden sie gezählt.  
- **Playerbots** werden gezählt (Player-Objekte).

**Linearer Faktor (Standard):**

**Mit `AllowAboveBase = 1`:**
### Beispiel (BaselinePlayers = 5)

| Spieler | Multiplier | Wirkung          |
|-------:|-----------:|------------------|
|      1 |       0,20 | 20 % HP/DMG      |
|      2 |       0,40 | 40 %             |
|      3 |       0,60 | 60 %             |
|      4 |       0,80 | 80 %             |
|      5 |       1,00 | 100 %            |
|      6 |       1,20 | 120 % *(nur wenn `AllowAboveBase=1`)* |

---

## Installation

1. Dieses Repo nach `azerothcore/modules/mod-fortis-autobalance` kopieren oder klonen.
2. Build & Install:
   ```bash
   cd /root/azerothcore/build
   cmake ..
   make -j"$(nproc)" worldserver
   make install

    //3. Konfiguration bereitstellen (falls nicht automatisch installiert):
   install -m 644 /root/azerothcore/modules/mod-fortis-autobalance/conf/mod_fortis_autobalance.conf.dist \
   /root/azerothcore/env/dist/etc/modules/mod_fortis_autobalance.conf.dist

    //4. (Optional) Eigene Konfiguration aktivieren:
   cp /root/azerothcore/env/dist/etc/modules/mod_fortis_autobalance.conf.dist \
   /root/azerothcore/env/dist/etc/modules/mod_fortis_autobalance.conf

   //5. Server neu starten:
   systemctl restart azerothcore.service
