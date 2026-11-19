# Einrichtung unter Windows

English version: [setup-windows.en.md](setup-windows.en.md).

Diese Anleitung führt von einem leeren PC bis zum laufenden Spiel, ganz ohne Vorkenntnisse. Du baust dabei alles selbst aus deinem eigenen Spiel.

**Das brauchst du:**
- *Pokémon Purpur* mit Update **4.0.0**, als Dump von deiner eigenen Konsole, und `prod.keys` von derselben Konsole. Wie du beides bekommst, steht hier nicht. Karmesin und andere Versionen sind nicht getestet.
- Windows und eine Grafikkarte mit aktuellem Vulkan-Treiber.
- Rund **30 GB freien Platz** während der Einrichtung. Danach bleiben etwa 12 GB belegt.
- Etwa 1 bis 2 Stunden, das meiste davon Wartezeit.

**Wichtig:** Dumps, Keys und der Ordner `game\` bleiben bei dir. Teile sie nie und lade sie nie hoch.

## 1. Terminal öffnen

Startmenü → **„Terminal“** eintippen → *Terminal* öffnen. Oben im Tab sollte „PowerShell“ stehen.

Alle Befehle unten kopierst du dort hinein und bestätigst sie mit Enter. Einfügen geht mit **Strg+V** oder Rechtsklick.

## 2. Programme installieren

Git, Python, MSYS2 (enthält den C-Compiler) und .NET 10 (für den Shader-Übersetzer). Bestätige Nachfragen mit `Y`.

```
winget install --id Git.Git -e
```

```
winget install --id Python.Python.3.14 -e
```

```
winget install --id MSYS2.MSYS2 -e
```

```
winget install --id Microsoft.DotNet.SDK.10 -e
```

Die Visual Studio Build Tools liefern den Linker für den Shader-Übersetzer. **Das lädt etwa 3–5 GB und dauert 10–20 Minuten.** Ein Installer-Fenster zeigt den Fortschritt und schließt sich am Ende von selbst:

```
winget install --id Microsoft.VisualStudio.BuildTools -e --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

MSYS2 aktualisieren. **Führe diesen Befehl zweimal aus**, denn der erste Durchlauf erneuert nur die Grundpakete und beendet sich dann:

```
C:\msys64\usr\bin\bash.exe -lc "pacman -Syuu --noconfirm"
```

Compiler und Bibliotheken installieren (etwa 1–2 GB):

```
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm make mingw-w64-ucrt-x86_64-clang mingw-w64-ucrt-x86_64-lld mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-sdl3 mingw-w64-ucrt-x86_64-opus mingw-w64-ucrt-x86_64-libvpx mingw-w64-ucrt-x86_64-zstd mingw-w64-ucrt-x86_64-vulkan-devel mingw-w64-ucrt-x86_64-vulkan-memory-allocator"
```

**Schließe jetzt das Terminal und öffne es neu**, damit Windows die neuen Programme findet. Dann prüfen:

```
python --version
```

Erwartete Ausgabe: `Python 3.14` oder neuer. Ältere Versionen funktionieren nicht.

```
dotnet --version
```

Erwartete Ausgabe: eine Zahl, die mit `10.` beginnt.

```
C:\msys64\ucrt64\bin\clang.exe --version
```

Erwartete Ausgabe: unter anderem `Target: x86_64-w64-windows-gnu`.

## 3. Projekt holen

Alles liegt unter `C:\KarPur`. Pfade ohne Leerzeichen ersparen später Ärger.

```
mkdir C:\KarPur
```

```
cd C:\KarPur
```

```
git clone https://github.com/Grunttanamo/KarPur.git repo
```

```
cd C:\KarPur\repo
```

Python-Umgebung einrichten:

```
python -m venv .venv
```

```
.venv\Scripts\python -m pip install -r tools\requirements.txt
```

Erwartete Ausgabe am Ende: `Successfully installed ...`

Ab jetzt startest du die Werkzeuge immer mit `.venv\Scripts\python`.

## 4. Deine Spieldateien eintragen

Lege die Dumps und `prod.keys` **außerhalb** des Repos ab, zum Beispiel in `C:\KarPur\dumps\`.

```
copy config.example.toml config.local.toml
```

```
notepad config.local.toml
```

Trage die **Pfade** zu Basisspiel, Update und `prod.keys` ein und speichere. Hast du keine DLC, schreibe `dlc = []`. Git ignoriert diese Datei, sie bleibt also bei dir.

## 5. Spieldateien entpacken

Programmdateien entpacken (etwa eine Minute):

```
.venv\Scripts\python tools\extract.py
```

Erwartete Ausgabe: ein Bericht zwischen zwei Linien aus `=====`. Steht dort eine Zeile mit `error:`, stimmt meist ein Pfad aus Schritt 4 nicht.

Systemschriftarten holen (ein paar Sekunden):

```
.venv\Scripts\python tools\extract_fonts.py
```

Erwartete Ausgabe: unter anderem sechs Zeilen von `font 0 standard: ... bytes` bis `font 5 nintendo-extension: ... bytes`.

Das Werkzeug hactool bauen, das Basisspiel und Update zusammenführt (unter einer Minute):

```
.venv\Scripts\python tools\build_hactool.py
```

Spieldaten zusammenführen. **Das dauert etwa 5–20 Minuten** und braucht zeitweise die rund 30 GB Platz:

```
.venv\Scripts\python tools\extract_romfs.py
```

Erwartete Ausgabe: wieder ein Bericht zwischen `=====`-Linien.

## 6. Shader-Übersetzer bauen

Er wandelt die Shader des Spiels in die Sprache um, die Vulkan versteht. Beim ersten Mal lädt .NET Pakete nach, **rechne mit 3–10 Minuten**:

```
.venv\Scripts\python tools\build_xlate.py
```

Erwartete Ausgabe am Ende: `built ...\build\xlate\totk_xlate.exe (... MB) in ... s`. Der Name stammt aus dem Projekt, aus dem der Übersetzer übernommen wurde.

## 7. Spiel übersetzen und bauen

Zuerst die Bildraten-Patches vorbereiten. **Lass diesen Schritt nicht aus, sonst läuft das Spiel nur mit 30 FPS:**

```
.venv\Scripts\python tools\fps_patches.py
```

Erwartete Ausgabe am Ende: `patch table: ... written to ...\build\fps_patches.txt`. Meldet das Werkzeug `sites differ`, ist es nicht Purpur 4.0.0.

Den Spielcode nach C übersetzen und kompilieren. **Das dauert 10–40 Minuten**, der PC wird dabei voll ausgelastet und kann laut werden. Zwischendurch siehst du Zeilen wie `125/400 files, 300s`:

```
.venv\Scripts\python tools\lift.py
```

Erwartete Ausgabe: ein Bericht, darin `Compiled: 404 of 404 files (0 errors)` oder ähnlich, mit 0 Fehlern.

`karpur.exe` bauen (etwa 5 Minuten):

```
.venv\Scripts\python tools\build_game.py
```

Erwartete Ausgabe am Ende: `build\game\karpur.exe: file ... MiB`.

Selbsttest (ein paar Sekunden, braucht keine Spieldaten):

```
build\game\karpur.exe --selftest
```

Erwartete Ausgabe am Ende: `selftest: passed (0 failures)`

## 8. Spiel starten

```
.venv\Scripts\python tools\run_with.py
```

**Gut zu wissen beim ersten Start:**
- Das Fenster öffnet sich hinter deinen anderen Fenstern, und das Symbol in der Taskleiste blinkt. Klicke darauf.
- Die Vulkan-Shader werden beim ersten Spielen nach und nach gebaut, anfangs kann es deshalb etwas ruckeln.
- **F11** schaltet das Vollbild um, **F10** die Leistungsanzeige, und **Esc** gibt die Maus frei. Ein Klick ins Fenster holt sie zurück.

Beenden kannst du das Spiel über das **X** am Fenster.

## 9. PC-Einstellungen im Hauptmenü einrichten

**Für Purpur 4.0.0 reicht ein Befehl.** Er kopiert die mitgelieferte Liste an ihren Platz:

```
copy tools\data\ui_patch_violet_4.0.0.txt game\ui_patch.txt
```

Beim nächsten Start steht dann **PC-Einstellungen** im Hauptmenü unter Options, und du kannst den Rest dieses Schritts überspringen. Für eine andere Spielversion geht es so:

Damit im Hauptmenü des Spiels (X) der Eintrag „PC-Einstellungen“ erscheint, braucht es eine Liste, die zu deinen Spieldateien passt. Sie bleibt unter `game\`. Bis dahin läuft das Spiel normal, die Einstellungen liegen dann im Menü auf **F1**.

```
.venv\Scripts\python tools\pc_menu.py
```

Beim ersten Mal meldet das Werkzeug `game/dump lacks the unpacked menu files`. Das ist normal. Starte das Spiel dann einmal so:

```
.venv\Scripts\python tools\run_with.py DATA_WATCH=1
```

Spiele so weit, bis du das Hauptmenü (X) öffnen kannst, öffne es und darin **Options**, und beende das Spiel. Führe dann das Werkzeug noch einmal aus:

```
.venv\Scripts\python tools\pc_menu.py
```

Erwartete letzte Zeile: `done: ...\game\ui_patch.txt`

Beim nächsten normalen Start (Schritt 8) steht **PC-Einstellungen** im Hauptmenü unter Options:

- **Bild:** Vollbild, Fenstergröße, FPS-Limit, Bildsynchronisation, Hochskalierung, Schärfe, Leistungsanzeige.
- **Pokémon-Anzahl**, also wie viele wilde Pokémon gleichzeitig unterwegs sind: Switch (15, Standard), Switch 2 (40), Enhanced (80) oder Extreme (120, kann die Bildrate stark drücken). Wirkt nach einem Neustart.
- **Steuerung:** Links oder rechts öffnet die Seite für Tastenbelegung, Controller, Vibration und Maus.

FPS-Limits über 60 sind experimentell: Effekte können ruckeln. Nach einer Änderung des Limits neu starten, damit das Laufen in Gebäuden flüssig bleibt.

Mit dieser Liste bleibt **F1** zu. Das ganze alte Menü, etwa für die Fehlersuche, bekommst du für einen Lauf so:

```
.venv\Scripts\python tools\run_with.py MENU_LOCK=0
```

## Auf den neuesten Stand bringen

Hol die Neuerungen und baue neu. Das geht meist viel schneller als beim ersten Mal:

```
cd C:\KarPur\repo
```

```
git pull
```

```
.venv\Scripts\python -m pip install -r tools\requirements.txt
```

```
.venv\Scripts\python tools\fps_patches.py
```

```
.venv\Scripts\python tools\lift.py
```

```
.venv\Scripts\python tools\build_game.py
```

Beende das Spiel vorher, solange es läuft, lässt sich `karpur.exe` nicht überschreiben.

## Wenn etwas nicht klappt

- **Spielstand:** Er liegt in `build\profile\save\`. Sichere den Ordner, bevor du größere Dinge ausprobierst.
- **Log des letzten Laufs:** `build\game\logs\karpur.log`. Die letzten 80 Zeilen zeigst du so an:

  ```
  Get-Content build\game\logs\karpur.log -Tail 80
  ```

- **Bericht schreiben:** Kopiere die genaue Fehlermeldung oder diese Log-Zeilen in ein [Issue auf GitHub](https://github.com/Grunttanamo/KarPur/issues). Die Berichte und das Log enthalten Namen, Zahlen und Adressen, aber auch Pfade von deinem PC, schau also kurz drüber. Dumps, Keys und Dateien aus `game\` gehören nie dazu.

Das Projekt ist noch in Arbeit. Abstürze und Grafikfehler können vorkommen; was offen ist, steht in [progress.md](progress.md).

## Selbst am Projekt mitarbeiten

Schalte einmal den Schutz ein, der Commits mit Spieldateien verhindert:

```
git config core.hooksPath .githooks
```

Vor jedem Commit:

```
python tools/check_content.py --staged
```

Erwartete Ausgabe: `check_content: OK`
