# Der PokiNative-Launcher

Mit dem Launcher baust du das Spiel ohne Befehle. Er braucht keine Installation und keine Administratorrechte, und er lädt nur Werkzeuge von deren offiziellen Seiten herunter. Deine Dumps, deine Schlüssel und das fertige Spiel bleiben auf deinem PC. Der Launcher ist auf Englisch, damit ihn alle nutzen können.

## Was du brauchst
- *Pokémon Karmesin* oder *Purpur* von deiner eigenen Konsole:
  - das **Grundspiel als `.xci` oder `.nsp`**. Bei `.nsp` brauchst du zusätzlich den Firmware-Ordner deiner Konsole mit den Systemschriften. Das gilt auch für eine aus `.nsp` umgewandelte `.xci`, falls ihr die Schriften fehlen.
  - das **Update 4.0.0 als `.nsp`**
  - das **DLC als `.nsp`**, wenn du es hast (optional)
- `prod.keys` von derselben Konsole
- Windows mit einer Grafikkarte, die Vulkan kann, und einem aktuellen Treiber
- Rund **30 GB freien Speicher** im Ziel-Ordner während des Baus. Danach bleiben etwa 12,5 GB belegt.
- Eine Internetverbindung für die Werkzeuge (rund 250 MB)

## So geht's
1. Starte `KarPur-Launcher.exe`.
2. Wähle im Tab **Setup** mit „Browse …“ die Dateien und einen **Target folder**, zum Beispiel `C:\Spiele\KarPur`.
3. Klick auf **Build**. Der erste Bau dauert je nach PC 30–60 Minuten. Dein PC ist dabei voll ausgelastet und kann laut werden. „Show details“ zeigt, was gerade passiert.
4. Danach startest du das Spiel mit **Play**.

Im Ziel-Ordner liegen danach:
- eine **Verknüpfung „Pokémon Violet (PokiNative)“** mit dem Spiel-Icon, zum Starten per Doppelklick
- eine Kopie von **`KarPur-Launcher.exe`** für Einstellungen, Reparatur und Updates
- der Ordner **`KarPur`** mit dem Spiel und allen Daten

Den Launcher kannst du jederzeit schließen oder den Bau mit **Cancel** abbrechen. Beim nächsten **Build** geht es an derselben Stelle weiter.

## RecompLauncher (zusätzlicher Startweg)

Der RecompLauncher-Eintrag unterstützt Windows und Linux. Das Manifest `recomplauncher.json` fragt das Grundspiel als `.xci`, das Update und `prod.keys` ab; DLC ist optional. Danach startet derselbe PokiNative-Bau ohne geöffnete Launcher-Oberfläche und RecompLauncher kann das Spiel direkt starten. Einstellungen, Spielstände und Logs sind ebenfalls eingebunden. Installationspfade mit Leerzeichen sind erlaubt.

RecompLaunchers derzeitiges Eingabeformat bietet keinen Firmware-Ordner an. Deshalb ist dort nur ein `.xci`-Grundspiel auswählbar; für ein `.nsp`-Grundspiel mit Firmware verwende im PokiNative Launcher den Tab **Setup**. Ein vollständiger Einrichtungs- und Spieltest über RecompLauncher steht noch aus.

## Bedienung mit Controller

Der Launcher versteht Xbox-kompatible Controller (XInput). Stick oder Steuerkreuz bewegen die Auswahl entsprechend der sichtbaren Anordnung. **A** öffnet eine Seite oder aktiviert den gewählten Knopf, **B** schließt eine Liste oder geht zur Seitenleiste zurück. **LB/RB** wechseln die Tabs, **Start** startet das Spiel. Links/rechts ändern Listen, Regler und Schalter. **X** öffnet bei Tastenbelegungen die zusätzlichen Optionen, etwa Default und None. Der türkisfarbene Rahmen zeigt die aktuelle Auswahl; das grüne **A** steht direkt am ausgewählten Tab. Nach dem Öffnen mit A verschwindet es, und links über Play erscheint **B Back**. Beim Zurückgehen verschwindet B wieder. Mausklicks und Scrollen blenden die Controller-Hinweise aus; die nächste Controller-Eingabe blendet die passende Anzeige wieder ein. Dateiauswahl und Texteingabe verwenden weiterhin Windows-Dialoge beziehungsweise eine Tastatur.

## DLC

Wenn du dein eigenes DLC im Setup angibst, wird es beim Bauen entpackt und beim Spielstart automatisch erkannt. Dafür ist kein zusätzlicher Schalter nötig. Ohne angegebene DLC-Datei wird kein Zusatzinhalt als vorhanden gemeldet. Der Maintainer hat bestätigt, dass die Freischaltung funktioniert und die DLC-Gebiete selbst auf einem Spielstand, der sie schon besucht hatte, richtig spielbar sind.

## Die Tabs
- **Neues Spiel und Render scale:** Bis die Charaktererstellung abgeschlossen ist, **1920×1080** als Render scale wählen. Bei anderen internen Auflösungen können die Figur und das Passbild falsch platziert sein oder fehlen. Danach kann man Render scale ändern und das Spiel neu starten. Die Fenstergröße kann währenddessen unabhängig gewählt werden.
- **Home:** ein durchgehender Banner mit deinem Hintergrundbild und dem Pokémon-Logo der Edition. Automatisch folgt das Design dem Spiel; unter **Launcher colour** kannst du Purpur oder Karmesin selbst wählen. Im Banner stehen Spielversion, DLC-Status und der letzte Start über diesen Launcher. Gibt es noch keinen protokollierten Start, steht dort stattdessen ausdrücklich der letzte Spielstand. **Play** und **Open game folder** stehen unten rechts; die Infofelder links teilen sich deren untere Kante. Bei schmalen Fenstern werden die Beschriftungen kompakter. Das Violet-Logo steht rechts und Miraidons Kopf bleibt durch den nach unten versetzten Bildausschnitt sichtbar; Scarlet behält Logo links und den bisherigen Bildausschnitt. Darunter bleibt **dein aktuelles Team** mit Namen, Level und Shiny-Markierung; das Spiel schreibt es beim Speichern in `team.json`. Die doppelten Infokarten und die Shader-Anzeige entfallen. Vor dem ersten Build zeigt die Seitenleiste das neutrale Icon; dieses Bild dient auch als Programm-Icon.

- **Setup:** Pfade, **Build** (bzw. **Rebuild**), **Repair** und **Cancel**. **Repair** stellt alle Projektdateien wieder her, richtet Python neu ein, prüft die Spieldaten und baut nur nach, was fehlt oder verändert wurde. Spielstände und Einstellungen fasst Repair nicht an.
- **Settings:** dieselben Einstellungen wie die PC-Einstellungen im Spiel: Vollbild, Fenstergröße, FPS-Limit, Bildsynchronisation, **Grafikkarte** (automatisch, oder eine bestimmte Karte, falls mehrere im PC stecken), **Modus** (Docked 1920×1080 in voller Qualität, oder Handheld 1280×720 für mehr Leistung auf schwächeren/AMD-Karten), Hochskalierung, Schärfe, Leistungsanzeige und Anzahl wilder Pokémon. Dazu DLAA, DLSS, Frame Generation (nur mit einer gewählten oder automatisch erkannten NVIDIA-Karte wählbar) und **AMD FSR 2**. FSR 2 kann die 3D-Szene in ihrer eigenen Auflösung glätten oder zur Fenstergröße hochskalieren; bewegte Pokémon, Pflanzen und Effekte können noch Nachzieher zeigen. FSR 1 und Bilinear bleiben wählbar. Hinzu kommen der **Kompatibilitätsmodus** (falls das Spiel nach der Charaktererstellung abstürzt) und die **Farbe des Launchers**: automatisch nach deinem Spiel (Lila für Purpur, Rot für Karmesin) oder fest gewählt. Der Button **Download DLSS from NVIDIA** lädt `nvngx_dlss.dll` erst auf Klick direkt aus NVIDIAs offizieller DLSS-Repo, prüft Größe und SHA-256 und legt sie neben `karpur.exe`. Die DLL ist nicht im Launcher enthalten; nach dem Download DLAA oder DLSS wählen und die Einstellungen speichern. AMD FSR 2 samt Vulkan-Shadern wird dagegen mit dem Launcher als Quellpaket geliefert und beim Einrichten direkt in das native Spiel eingebaut; dafür ist kein gesonderter Download nötig.
- **Ambient occlusion / Indirect light** sind getrennte GPU-Effekte mit Off/Low/Medium/High. Beide sind standardmäßig aus. Sie bearbeiten das Szenenbild vor dem HUD; Ambient Occlusion ergänzt Kontaktschatten, Indirect light eine einfache Bildschirmraum-Annäherung für reflektiertes Licht. Sie können Bildrate und Bildqualität beeinflussen und benötigen eigene PC-Grafikressourcen. Die Speicher-Pools des Spiels und die Schatten-Zuweisung bleiben unverändert.
- **Compatibility** bietet zwei unabhängige Schalter: Der bisherige Modus schreibt `submit_defer = 0`; **Keep hot memory active** schreibt `hot_cool = 0`. Nach dem Ausschalten schreibt der zweite Schalter wieder `hot_cool = 300`, den bisherigen Standard. Er verhindert das Abkühlen bestimmter Speicherbereiche und kann bei längeren Spielsitzungen Leistung kosten. Änderungen wirken beim nächsten Spielstart.
- **Resizable BAR:** Auf unterstützten PCs kann es PokiNative beschleunigen. Im UEFI/BIOS des Mainboards **Above 4G Decoding** und **Re-Size BAR Support** (teils **Smart Access Memory**) aktivieren, im UEFI-Modus starten und den PC neu starten. Bei NVIDIA unter **NVIDIA Systemsteuerung → Systeminformationen → Resizable BAR** prüfen, ob „Yes“ angezeigt wird. Die Menünamen hängen vom Mainboard ab; [Intels Anleitung](https://www.intel.com/content/www/us/en/support/articles/000090831/software.html) und [NVIDIAs Prüfung](https://www.nvidia.com/en-us/geforce/news/geforce-rtx-30-series-resizable-bar-support/) erklären die allgemeinen Schritte. Der Leistungsgewinn hängt von der Hardware ab.
- **People shown** bestimmt, wie viele fest platzierte Stadt-NPCs gleichzeitig ein Modell bekommen: Original 12, dann 24, 36 oder 48. Diese Auswahl ist von der Anzahl wilder Pokémon und der Modell-Detailentfernung getrennt. Ein ungültig gewordener GlobalHeap-Zähler wird korrigiert; Speicher-Pools werden nicht vergrößert. Höhere Zahlen können bei echtem Speicherdruck weiterhin Qualität oder Leistung kosten.
- **People detail / Pokémon detail** reichen bei Menschen von Original 8 m bis 80 m und bei Pokémon von Original 10 m bis 80 m. Sie legen eine Mindestentfernung für das detaillierte Modell fest; ein aktiver LOD-Mod kann bereits größere Werte liefern. Sie ändern nicht, ab wann eine Figur erscheint oder gespawnt wird. Original ist voreingestellt. Die ersten vier Stufen wurden im Spiel bestätigt; die größeren Entfernungen werden noch geprüft.
- **Render scale** legt bei unterstütztem Pokémon Purpur 4.0.0 die Auflösung fest, in der das Spiel seine Bilder zeichnet. Die 16:9-Stufen 480p, 720p, 1080p, 1440p und 2160p wurden vom Maintainer im Spiel geprüft. Neu ist 450p (800×450); diese Stufe braucht noch einen Spieltest. Experimentell sind 21:9 und 32:9 sowie die neuen Formate 4:3 (1440×1080), 3:2 (1620×1080), 16:10 (1920×1200) und 9:16 Hochkant (1080×1920). Bei breiteren Bildern erweitert die Kamera den seitlichen Ausschnitt, bei höheren Bildern den vertikalen. Die Spieloberfläche bleibt vorerst in einem mittigen 16:9-Bereich; LOD, Sprachblasen und einzelne Menüs können bei anderen Formaten noch fehlerhaft sein. **Window size** bestimmt unabhängig davon die Größe auf dem Bildschirm; für 9:16 gibt es auch ein kleineres 720×1280-Fenster. Der GPU-Speicherpool des Spiels und die Schatten-Speichergrößen bleiben unverändert. Wenn der Build die nötigen Patches nicht enthält, ist die Auswahl deaktiviert.
- **Controls:** Tastatur, Maus und Controller. Knopf anklicken, dann die gewünschte Taste drücken; Rechtsklick setzt Standard oder „keine“. Dieselbe Seite öffnet sich im Spiel selbst mit **F1** oder **Esc**; dort stehen zusätzlich die **Leistungsanzeige** (Aus/FPS/Voll) und, über den Y-Knopf unten links, **Spiel beenden** (mit Rückfrage).
- **Saves:** dein Spielstand. **Export…** speichert ihn als `.zip` (zum Sichern oder für einen anderen PC), **Import…** holt einen solchen Spielstand zurück, **Open folder** öffnet den Ordner. Unter **Backups** kannst du einen der letzten Spielstände wiederherstellen. Bei Import und Wiederherstellen legt der Launcher den aktuellen Spielstand vorher als Backup ab, es geht also nichts verloren. Das Spiel muss dabei geschlossen sein.
- **Mods (experimentell):** deine eigenen Mods, z. B. von NexusMods. Bisher sind nur wenige Mods ausprobiert; ein Mod kann das Spiel abstürzen lassen oder seltsam verhalten. Dann den Mod ausschalten und im Feedback melden, welcher es war. **Add mod folder…** nimmt einen Mod-Ordner auf, **Add .zip…** entpackt ein Archiv in den Mods-Ordner. Jeder Mod hat einen Schalter. Es wird nichts an deinem Spiel ersetzt: PokiNative reicht dem Spiel die Datei des Mods, wenn es die Originaldatei lädt. Ausgeschaltet ist der Mod beim nächsten Spielstart wieder weg. Für große Texturpakete kannst du optional eine maximale Dateigröße und **Only .bntx texture replacements** wählen; die Liste wird dann neu erstellt und zeigt die Zahl ausgewählter und ausgelassener Dateien. Der Reporter von #74 testete 16 MiB zusammen mit BNTX-only erfolgreich, aber das vollständige Paket kann weiterhin den Speicher des Spiels erschöpfen. Ohne Filter bleibt die bisherige Mod-Auswahl erhalten. Neue Dateien, die ein Mod hinzufügt, und Code-Mods (`exefs`) gehen noch nicht.
- **Feedback:** ein Bericht, wie PokiNative bei dir läuft. Launcher-Version, Spiel, Update, DLC, Grafikkarte, Prozessor, Arbeitsspeicher (Typ und Größe) und Windows-Version füllt der Launcher selbst aus, du kannst alles ändern. Dazu kommt ein Textfeld (bis 5000 Zeichen). „Attach log“ hängt das Ende des Spiel-Logs an, dabei werden die Ordnernamen deines Benutzerkontos weggelassen. **Open on GitHub** öffnet den fertig ausgefüllten Bericht als GitHub-Issue im Browser; dort prüfst du ihn und schickst ihn ab. Dafür brauchst du ein kostenloses GitHub-Konto. Die Berichte sind öffentlich. **Copy report** kopiert den Bericht, zum Beispiel für Discord.
- **Update:** sucht auf der GitHub-Releases-Seite nach einer neuen Version. **Download and install** ersetzt den Launcher und baut danach nur, was sich geändert hat. Meist dauert das wenige Minuten.

## Was der Launcher herunterlädt
- Python (offizielles Paket von python.org) und fünf Python-Pakete von PyPI
- den Compiler: Clang und die Bibliotheken des Spiels als Pakete von MSYS2 (mirror.msys2.org)

Jede Datei hat eine feste Prüfsumme. Eine veränderte Datei lehnt der Launcher ab.
Die optionale NVIDIA-DLSS-DLL wird nur nach Klick auf den Button heruntergeladen und ebenfalls vor dem Einsetzen geprüft.

## Wenn etwas nicht klappt
- Der Launcher nennt den Fehler. Das vollständige Protokoll steht in `KarPur\launcher.log` im Ziel-Ordner, die Ausgaben der Werkzeuge in `KarPur\build\setup.log`.
- Hilft das nicht, probier **Repair**.
- Die Protokolle enthalten keine Schlüssel. Teile trotzdem nie deine Dumps oder `prod.keys`.

## Für Entwickler
- **Launcher bauen:** `tools/make_launcher.py`. Das braucht das .NET 10 SDK, den Shader-Übersetzer und hactool. Die Version steht in `launcher/KarPurLauncher.csproj`.
- **Bau-Skript:** `tools/setup_build.py`. Es lässt sich auch ohne Launcher starten. Es baut nur Schritte nach, deren Ergebnis fehlt oder deren Eingabedateien sich geändert haben (`STEP_INPUTS`).
- **Versionen der Downloads:** `tools/launcher_pins.py` (Python) und `tools/toolchain_msys2.py pin` (Compiler) legen sie fest.
- **Update veröffentlichen:** Release mit Tag `vX.Y.Z` und der Datei `KarPur-Launcher.exe` anlegen. Vorher die Version im `.csproj` erhöhen.
- **Menü-Liste:** `tools/data/ui_patch_violet_4.0.0.txt` ist die Liste hinter den PC-Einstellungen im Spielmenü. Sie enthält nur Größen, Prüfsummen, Positionen, unsere Werte und unsere Texte.

## Lokale Vorschau in preview2

Die Test-Version entsteht mit `tools/make_launcher.py --test` unter `launcher\bin\preview2\KarPur-Launcher.exe`; `dist` bleibt für Veröffentlichungen frei. Beim Start aus diesem Ordner wählt sie den aktuellen Repo-Stand als Target folder und verwendet dessen `build\game\karpur.exe`. Alle Setup-Pfade sowie Build, Repair und Update bleiben normal bedienbar. Der Target folder kann manuell geändert werden: ein Quell-Repo wird direkt verwendet, ein Installationsziel enthält wie bisher den Unterordner `KarPur`.

Für einen Test-Build außerhalb des Quellordners: `tools/make_launcher.py --out "C:\KarPur\tests\0.4.1"`. Der Launcher und `recomplauncher.json` liegen dann direkt dort; Payload und .NET-Zwischendateien liegen im Unterordner `.build`. Zusätzlich erzeugt der Build `KarPur-Launcher-Windows-x86_x64.exe` samt SHA-256-Datei; `KarPur-Launcher.exe` bleibt für RecompLauncher und bestehende Installationen erhalten.

Die Windows-Manifestdatei im Projektstamm verweist auf den tatsächlichen Windows-Spielstandordner `{install}/../KarPur/build/profile/save`. Der Linux-Paketbau erzeugt eine eigene Manifestkopie mit `{install}/build/profile/save`, da der Linux-Launcher direkt unter `{install}` baut. `saves`-Einträge in RecompLauncher sind alle aktive Speicherorte und keine Fallback-Liste; alternative Plattformpfade gehören daher nicht gemeinsam in dasselbe Array.

Einstellungen, Team, Spielstände und Mod-Liste gehören zum gewählten Ziel. Es werden keine Mod-Listen zwischen verschiedenen Builds kopiert. Lokale Builds werden beim Öffnen nicht automatisch neu gebaut; Build oder Repair startet der Maintainer selbst. Die zuvor eingeführten Workspace-Sperren und die Sidecar-Konfiguration werden nicht mehr verwendet.
