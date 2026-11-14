# Vor jedem Launcher-Release

Der Launcher ist der Weg, auf dem gespielt wird. Ein Fehler auf diesem Weg trifft alle gleichzeitig, und er lässt
sich aus dem Launcher heraus meistens **nicht** reparieren — wer in einem halb entpackten Toolchain-Ordner feststeckt,
kommt ohne Hilfe nicht mehr heraus. Deshalb diese Liste.

Wichtigste Regel aus allem, was bisher schiefgegangen ist:

> **Jede Änderung einmal über den Launcher testen, nicht über die Umgebungsvariable.**
> Die Variable umgeht die Einstellungstabelle. Ein Fehler, der nur ohne Variable auftritt, tritt bei jedem
> Launcher-Start auf — und bei keinem Test von der Kommandozeile.

## 1. Automatisch prüfen

```bash
cd C:\KarPur\repo; .venv\Scripts\python tools\check_launcher.py
```

Fünf Prüfungen, alle statisch, ohne etwas zu bauen. Jede steht für einen Fehler, der wirklich passiert ist:

| Prüfung | Was sie prüft | Warum |
|---|---|---|
| `settings` | Jeder Schlüssel, den die Laufzeit mit `cfg_value`, `cfg_int`, `cfg_double` oder `cfg_bool` liest, hat eine Zeile in der Einstellungstabelle von `runtime/cfg.c`. | Ohne Zeile beenden diese vier das Spiel, sobald die Variable nicht gesetzt ist. Das ist jeder Launcher-Start. Hat den Launcher am 13.11. komplett lahmgelegt; zwei weitere Fundstellen am 14.11. Für Diagnoseschalter gibt es `rt_knob`, das braucht keine Zeile. |
| `keys` | Jeder Schlüssel, den der Launcher in `karpur.ini` schreibt, und jede Zeile der Tabelle steht in der `known`-Liste. | Ein unbekannter Schlüssel wird stillschweigend ignoriert — die Einstellung tut dann einfach nichts. |
| `tools` | Jedes Skript, das der Build startet, existiert und lässt sich im isolierten Modus importieren (`python -I`). | Issue #45: ein Build lief 75 Minuten und brach dann mit `No module named 'romfs_index'` ab, weil Python den Skriptordner nicht auf den Suchpfad legt, wenn `PYTHONSAFEPATH` gesetzt ist. |
| `toolchain` | Die Programme, die der Build aufruft, sind die, nach denen `toolchain_msys2.TOOLS` nach dem Entpacken sucht. | Issue #62: ein abgebrochener Toolchain-Download wurde als fertig verbucht, weil nur `clang.exe` geprüft wurde — und clang ist Paket 1 von 31. Jeder weitere Build scheiterte danach mit „ninja not found", ohne Ausweg. |
| `payload` | Die Dateien, die ein Build auf dem PC des Spielers braucht, werden von `tools/make_launcher.py` auch mitgeliefert (sind also nicht von git ignoriert). | Eine ignorierte Datei fehlt im Release und niemand merkt es beim Entwickeln, weil sie lokal ja da ist. |
| `project` | Die Bauoptionen des Launchers: `SelfContained` an, `InvariantGlobalization` **aus**. | 14.11.: mit `InvariantGlobalization` an wirft WinForms eine unbehandelte Ausnahme, sobald man die Tastaturbelegung wechselt, während ein Textfeld den Fokus hat — es fragt dann das System nach der Kultur der Belegung, und die gibt es im invarianten Modus nicht. 0x0407 ist Deutsch. |

Schlägt eine Prüfung fehl, nennt sie die Fundstellen. **Kein Release mit einer fehlgeschlagenen Prüfung.**

## 2. Von Hand prüfen

Das kann kein Werkzeug abnehmen.

**Ein vollständiger Build aus einem leeren Ordner**, nicht nur ein Weiterbau im gewachsenen Arbeitsordner. Nur
dabei laufen die Schritte `toolchain`, `extract`, `fonts` und `icon` überhaupt — im Alltag sind die längst erledigt
und werden gar nicht mehr eingeplant.

**Der Shader-Vorbau.** In der Log muss stehen:

```
gfx_shader: prepared <N> shader variants ... 0 translations failed
```

Schlägt dieser Schritt fehl, wird das nur protokolliert und der Build gilt als erfolgreich — das Spiel baut die
Shader dann beim Spielen und ruckelt die erste Stunde. Also wirklich nachsehen, nicht auf „DONE" vertrauen.

**Der Selbsttest.** Der Schritt `selftest` läuft `karpur.exe --selftest`. Er muss durchlaufen.

**Jede neue Einstellung einmal im Launcher umschalten**, speichern, Spiel starten, und in der Log nachsehen, dass
der Wert angekommen ist. Dann wieder zurückstellen und prüfen, dass auch das ankommt. Siehe die Regel oben.

**Einmal spielen**, mindestens:

- NPCs in einer Stadt — sie müssen bis etwa **30 m** sichtbar sein. Das ist der Prüfstein dafür, dass dem Spiel
  nichts von seinem Streaming-Budget genommen wurde (13.11., und am 14.11. noch einmal bei Schatten x4).
- **Die Karte öffnen**, bei der höchsten Schatteneinstellung. Hat x4 in 0.3.5 zuverlässig abgestürzt (Issue #61).
- Ein Kampf mit Ballwurf, ein Gebietswechsel.

**Platz auf der Platte:** etwa 30 GB werden gebraucht. Der `check`-Schritt sagt es, aber danach läuft der Build
Stunden — besser vorher nachsehen.

## 3. Wenn ein Spieler etwas meldet

Die Fehlermeldung des Launchers nennt seit dem 14.11. die Zeile, die den Fehler verursacht hat, plus die zwei
danach — nicht mehr die letzten vier Zeilen der Ausgabe. Ein Build, der an einer Datei scheitert, übersetzt die
anderen nämlich weiter, und die letzten Zeilen sind dann Warnungen von woanders.

Zum Auswerten:

- `tools/symbolize.py` — einen Absturzbericht hineingeben, es nennt die Funktionen aus dem eigenen Dump.
  Der eigene Code des Spiels ist entfernt, nnSdk und das subsdk nicht, und genau dort steckt oft der Fehler.
- `tools/flight_view.py` — eine Flugschreiber-Aufnahme (`TOTK_FLIGHT=1 TOTK_FLIGHT_THUMBS=0
  TOTK_FLIGHT_SPIKE_MS=40`) auswerten: was die langsamen Bilder gekostet hat und woran.
  `TOTK_FLIGHT_THUMBS=0` ist wichtig, sonst liegen Bilder aus dem Spiel in der Aufnahme.

## 4. Wenn etwas doch durchkommt

Dann gehört eine Zeile in die Tabelle unter Punkt 1 und eine Prüfung in `tools/check_launcher.py`. Jede Zeile dort
steht für einen Fehler, den ein Spieler gefunden hat, statt wir.
