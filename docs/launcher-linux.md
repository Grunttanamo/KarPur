# KarPur Launcher für Linux

Diese separate Linux-Fassung verwendet Avalonia und wird als `KarPur-Launcher-Linux-x86_64.AppImage` gebaut.
Für das Steam Deck die eindeutig benannte Datei `KarPur-Linux-Launcher-0.3.8.AppImage` verwenden. Es genügt,
diese eine AppImage-Datei zu übertragen; den `linux-x64`-Ordner nicht starten.
Zusätzlich gibt es `KarPur-Launcher-Linux-x86_64.tar.gz` für den Start aus einem vollständigen Linux-Ordner.
Sie bietet Installation und Reparatur aus eigenen Dumps, den Start einer nativen Linux-Spielinstallation,
Grafikeinstellungen, Spielstand-Import/Export mit Sicherung und ein Build-Protokoll. Die Übersicht verwendet die
vorhandenen, vom Maintainer gezeichneten Editionsbilder und Logos. Die Linux-Version 0.3.8 verwendet eine englische
Oberfläche, eine eigene Controls- und Update-Seite sowie ein Editionslogo im Banner.

**Experimentell:** Auf dem Steam Deck fehlen teils Bäume oder andere Objekte; auch die Leistung kann schwanken.
Der Hinweis auf der Startseite verlinkt GitHub-Issues und Pull Requests. Berichte von Linux- und Vulkan-Kennern
mit Gerät, Grafiktreiber und Protokoll helfen, die Ursache einzugrenzen.

## Starten

Diese erste Ausgabe benötigt Linux auf x86-64 mit glibc ab 2.38, etwa Ubuntu 24.04 oder neuer.
Die Grenze stammt aus der mitgelieferten SDL3-Bibliothek. Die AppImage-Datei ausführbar machen.

```sh
chmod +x KarPur-Linux-Launcher-0.3.8.AppImage
```

Dann starten.

```sh
./KarPur-Linux-Launcher-0.3.8.AppImage
```

Wer das Ordnerpaket nutzt, entpackt es vollständig und startet `linux-x64/KarPur-Launcher-Linux`.
`linux-x64/payload.zip` muss dabei direkt neben der Programmdatei liegen. Ein einzelner aus `bin/Release`
kopierter Programmstarter kann das Spiel nicht installieren.

Falls das System kein FUSE-Mount erlaubt, kann der AppImage-Laufzeitkern die Anwendung auch entpackt starten.

```sh
./KarPur-Linux-Launcher-0.3.8.AppImage --appimage-extract-and-run
```

## Spiel einrichten

Unter **Setup** einen eigenen Installationsordner wählen. Basisspiel, Update und `prod.keys` werden aus den
lokalen Dateien ausgewählt. Ein Basisspiel als NSP benötigt zusätzlich einen Ordner mit den System-NCAs der eigenen
Konsole. Ein DLC als NSP ist optional. **Install / update / repair** übernimmt das eingebettete
Quellpaket und führt die Build-Schritte aus. Fertige Schritte werden beim nächsten Versuch wiederverwendet.

Der Launcher bringt Python 3.14, dessen benötigte Pakete, SDL3, hactool, den Shader-Übersetzer, einen portablen
Zig/Clang-Compiler, Ninja und die benötigten Entwicklungsbibliotheken für Vulkan, Opus, VPX und Zstd mit. Für die
Installation auf dem Steam Deck sind keine `apt`- oder `pacman`-Befehle nötig. Der erste Build benötigt weiterhin
genügend Zeit und freien Speicherplatz. Die Grafikausgabe verwendet den Vulkan-Treiber des Systems.

Der Zielordner braucht mindestens 30 GB freien Platz. Spiel, erzeugter Code und Spielstände liegen in diesem
beschreibbaren Ordner. Die Einstellungen des Launchers werden unter `$XDG_CONFIG_HOME/karpur-linux/launcher.json`
beziehungsweise `~/.config/karpur-linux/launcher.json` gespeichert. Bei der Auswahl werden nur Dateipfade gespeichert.

Nach einem erfolgreichen Build startet **Play** die Datei `build/game/karpur`. Der Launcher legt im Zielordner eine
startbare Kopie `KarPur-Linux-Launcher.AppImage` sowie einen Eintrag im Anwendungsmenü und eine Desktop-Verknüpfung an.
Bei einer vorhandenen Linux-Installation kann ihr Projektordner unter **Setup** ausgewählt werden. Der Zielordner
muss dieselbe Struktur mit `build/game` und `build/profile` verwenden.

## Steam Deck Steuerung

Die Seite **Controls** aktiviert auf SteamOS standardmäßig die Deck-Steuerung. Das Spiel wählt die Positionen der
Nintendo-Tasten: Deck B → Switch A, A → B, Y → X und X → Y. Links bewegt die Figur, rechts die Kamera. L4 öffnet das
PC-Menü, R4 ist Plus und R5 Minus, sofern SDL die Rücktasten des Deck-Controllers liefert. Bei einem erkannten
Controller werden die vom Desktop-Modus zusätzlich erzeugten Tastatur- und Mausaktionen für das Spiel ignoriert.
Die **Controls**-Seite kann den Modus abschalten und Stick- sowie Kameraoptionen ändern. Die reale Deck-Eingabe muss
auf dem Gerät geprüft werden; der lokale Linux-Test besitzt keinen Steam-Deck-Controller.

## Updates

**Update** sucht in den öffentlichen GitHub-Releases nach einem Linux-AppImage. Sobald eines veröffentlicht wird,
kann es mit dem Launcher heruntergeladen, bei vorhandener SHA-256-Datei geprüft und als startbare Kopie im
Installationsordner eingesetzt werden. Anschließend den Launcher neu starten. Bis ein Linux-AppImage veröffentlicht
ist, zeigt die Seite dies ausdrücklich an.

## Bestehenden GitHub-Release ergänzen

Auf der Release-Seite von `v0.3.8` den Stift **Edit** öffnen. Unter den vorhandenen Windows-Dateien
`KarPur-Linux-Launcher-0.3.8.AppImage` und `KarPur-Linux-Launcher-0.3.8.AppImage.sha256` als weitere Dateien
hochladen und **Update release** wählen. Die Windows-Datei bleibt dabei erhalten. In der Beschreibung den Linux-Port
als experimentell kennzeichnen und auf GitHub-Issues und Pull Requests verweisen. Dazu zuerst das neue AppImage direkt
starten; beim Öffnen einer vorhandenen Installation wird die bisherige Desktop-Verknüpfung auf diese Fassung erneuert.

Die automatisch angebotenen GitHub-Archive **Source code (zip/tar.gz)** bleiben an den alten `v0.3.8`-Tag gebunden.
Für Linux-Quellen im Archiv braucht es einen späteren Tag nach dem Commit der Linux-Dateien. Den bestehenden
veröffentlichten Tag dafür nicht verschieben.

## Einstellungen und Spielstände

Die Einstellungen schreiben `build/game/karpur.ini` und erhalten vorhandene Kommentare und weitere Werte.
Docked/Handheld bestimmt die interne Spielauflösung. Die Fenstergröße bestimmt die Ausgabegröße.
Die früheren Steam-Deck-Testschalter für `MEMORY=import` und die Compute-Shader-Übersetzung sind entfernt. Alte
Werte aus `launcher.json` beeinflussen den Spielstart nicht mehr. Ein Neubau des Spiels ist dafür nicht nötig.

Der Spielstand-Export verwendet ein ZIP mit `main` auf oberster Ebene. Der Import prüft das Archiv zuerst und
sichert den aktuellen Stand unter `build/profile/save-backup`. Import und Export sind bei laufendem Spiel gesperrt.
Ein noch nicht übernommener Speicher-Commit muss vor dem Export einmal vom Spiel übernommen werden.

## AppImage selbst erstellen (nur für Entwickler)

Zum Installieren des Spiels ist dieser Abschnitt nicht nötig. Wer das AppImage selbst neu baut, braucht auf dem
Build-System .NET 10 SDK, C/C++-Werkzeuge, CMake, Git, Python und `squashfs-tools`.

```sh
python3 tools/make_appimage.py
```

Der Vorgang lädt die gepinnten Python-, SDL3-, Zig- und AppImage-Bestandteile mit SHA-256-Prüfung, baut die nativen
Hilfsprogramme und veröffentlicht den Launcher mit seiner .NET-Laufzeit. Das Ergebnis und seine Prüfsumme stehen
unter `dist/linux`. Das vollständige Ordnerpaket wird aus denselben gebündelten Dateien gebaut. Das Quellpaket wird
aus der Dateiliste des Projekts erstellt. Game-, Build- und Generated-Dateien
werden dabei ausgeschlossen, außer den ausdrücklich benötigten selbst gebauten Hilfsprogrammen.

Unter WSL2 funktioniert derselbe Build. Diese Fassung ist eigenständig in `launcher-linux/` und verwendet nur den
plattformunabhängigen INI-Leser des bestehenden Launchers. Mod-Verwaltung und dessen Controller-Navigation für die
Launcher-Oberfläche sind in dieser Linux-Fassung noch offen.

Die native Violet-Runtime hatte vor dieser Arbeit noch keinen bestätigten vollständigen Linux-Spieldurchlauf.
Launcher-Prüfungen mit künstlichen Daten ersetzen diesen Spieldurchlauf nicht. Verifizierte Ergebnisse dieses
Builds werden in `docs/progress.md` festgehalten.

Die Paketstruktur folgt den Hinweisen von [Avalonia zur Linux-Bereitstellung](https://docs.avaloniaui.net/docs/deployment/linux)
und verwendet den [AppImage Type 2 Runtime](https://github.com/AppImage/type2-runtime).
