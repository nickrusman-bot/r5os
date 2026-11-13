Korte prompts. Geef ze één voor één, elk pas na het rapport van de vorige. Stap 2 is gemerged (PR #4247, 13 nov). Elke stap krijgt een eigen tak vanaf `origin/main` en een eigen PR, pas nadat de vorige stap gemerged is; niet gestapeld.

---

## Stap 3a (alleen papier)

Voer stap 3a uit van `generated/gpu-m11/BRIEF.md`: het ontwerp voor vensters zonder kopie. Alleen documentatie: geen code, geen jam. Stap 2 (de compositor-kern, PR #4247) zit al in `main`: ontwerp op wat daar echt gebouwd is (`distro/app_server/Compositor.cpp`, `docs/APP-SERVER-COMPOSITOR.md`, "Decided in step 2"), niet op het oude ontwerp. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan besluit 15 in de brief, en `docs/APP-SERVER-COMPOSITOR.md`, "Zero-copy windows" en "Decided after step 0", punt 3. Je eerste antwoord: de regels die gelden en je plan. Wacht op mijn "ga".

Werk besluit 15 uit zoals stap 3a in de tabel zegt. Kijk in de echte bron, niet uit je geheugen: de R5-interface (`r5_gpu.h`, de kernel, de R5 DRM-bibliotheek), Mesa (hoe RADV een fd importeert, en `wsi_common_haiku.cpp`) en de Haiku-boom in WSL (veilige willekeur in de kernel, de afzender van een port-bericht). Past de stap van token naar lokale fd niet zonder een RADV-wijziging: stop en vraag. Waar de bron iets anders zegt dan besluit 15: zet het onder "Open questions", niet zelf kiezen.

Eén documentatie-PR vanaf `origin/main`, tak `claude/m11-zero-copy-design`, alleen na mijn ja. Meld kort en stop.

---

## Stap 3b

Voer stap 3b uit van `generated/gpu-m11/BRIEF.md`: Vulkan-vensters zonder kopie (A2) en de scanout in Vulkan, in één keer. Alleen 3b. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan besluit 15 en 15b in de brief en in `docs/APP-SERVER-COMPOSITOR.md` het hele 3a-ontwerp (gemerged in #4251 en #4252), ook "The scanout import into Vulkan". Je eerste antwoord: de regels die gelden, het plan per laag, en wat je niet in QEMU kunt testen. Wacht op mijn "ga".

Bouw precies het ontwerp, geen call of nummer dat er niet in staat:
- kernel: `BO_EXPORT` / `BO_IMPORT` (`+ 17`, `+ 18`), het exportnummer met de rechtencontrole (team vastgelegd bij export, alleen app_server, één keer, 5 s, 32 per bestand), de splitsing van plek en geheugen met de teller, `BO_PRESENT_MARK` (`+ 32`), de grenzen (1024, 512 per bestand, 64 als reserve voor app_server) met de logregel bij een volle grens;
- R5 DRM-bibliotheek: PRIME via een lokale fd, `GEM_OP` en `GEM_METADATA`; de vijfde libdrm-patch voor de grootte; libdrm opnieuw gebouwd;
- WSI: een eigen `AppServerLink` (geen wijziging in libbe), lineaire beelden, een semafoor per beeld, `VK_ERROR_OUT_OF_DATE_KHR` na de timeout; een nieuw `mesa_radv`-pakket met de pin en de libdrm-symboolcheck;
- app_server: de import met de controle van grootte en semafoor tegen `ClientTeam()`, "klaar" met `FENCE_WAIT` timeout 0, "weer vrij" met `release_sem_etc`, en de reset zoals in het ontwerp;
- het testprogramma `r5-share-test` met de pass- en failregels uit het ontwerp;
- de scanout in Vulkan: de export van de scanout naar app_server zelf, de import, een `vkCmdCopyBuffer` van de gewijzigde rijen in dezelfde submit, een nieuwe import na een moduswissel. Het vervangt de CPU-kopie naar de back buffer en de M8-`DMA_DATA`-kopie alleen als het niet slechter meet; beide wegen blijven kiesbaar tot de stick het laat zien, en beide tijden komen in het rapport. Een aparte commit binnen deze stap.

Testen, eerlijk:
- In QEMU en VirtualBox start `radeon_hd` niet; daar draait alleen het kopiepad op Lavapipe (`zero-copy off: lavapipe`) en moet het bureaublad werken zoals na stap 2.
- De logica van nummers, rechten, verlopen, grenzen en de teller zet je in losse functies met een hosttest, zoals `scripts/test-r5-gpu-checks.cpp` in M10. Die moet slagen.
- `r5-share-test`, het echte pad zonder kopie en de scanout-import draaien pas op de 5625U (de stick); schrijf dat als "Onbewezen".

Vóór de jam: SMAP-review van de diff en de QEMU-SMAP-boot. Eigen tak `claude/m11-zero-copy` vanaf `origin/main`. Vraag vóór je jamt, pusht en een PR opent. Meld kort en stop.

---

## Stap 4

Voer stap 4 uit van `generated/gpu-m11/BRIEF.md`: de schaduw (A3), het frosted glas (A4) en de twee vinkjes. Alleen stap 4 (animaties zijn 4b). Begin pas als 3b (#4255) in `main` zit. Lees eerst `AGENTS.md` helemaal en de "Rules for the agent". Dit is ook UI-werk: volg de alinea onder LibR5 in `AGENTS.md` (eerst `docs/LIBR5.md`, `docs/APPEARANCE-KIT.md`, R5Book en één bestaande Settings-pagina). Dan besluit 2, 8, 13 en 16 in de brief, en in `docs/APP-SERVER-COMPOSITOR.md` "The compose pass", "What programs and developers see", "Settings" en wat stap 2 en 3b echt bouwden ("Decided in step 2", "Decided in step 3b"). Je eerste antwoord: de regels die gelden, het plan per deel, en wat je niet in QEMU kunt testen. Wacht op mijn "ga".

**Eerst twee dingen op papier, dan stoppen:**
1. **Het alfakanaal (besluit 16).** Painter schrijft in een gewone 32-bits buffer geen betrouwbare alfa. De compositor gebruikt doorzichtigheid daarom alleen binnen het glasgebied (de tab, de kaderlijn, en views die om glas vroegen); daarbuiten is elk pixel ondoorzichtig, wat er ook in de alfa staat. Kijk in de bron welke tekenroutines van Painter in de `B_ALPHA_COMPOSITE`-modi goede alfa schrijven (tekst, vlakken, lijnen, bitmaps, gradiënten). Schrijf op: klein genoeg om glas-views (`set_view_glass()`) nu goed te doen, of te groot, dan komen glas-views in M12 en krijgt M11 glas op de tab en de kaderlijn (de call bestaat dan wel, wordt bewaard, en werkt vanaf M12).
2. **Het uiterlijk.** Twee of drie varianten van schaduw (grootte, zachtheid) en glas (blurniveau, doorzichtigheid van de tab), elk als schermafbeelding in QEMU op Lavapipe van hetzelfde bureaublad, met een actief en een inactief venster. Naast elkaar, met de waarden erbij.

Leg beide aan mij voor en stop. Ik kies; daarna bouw je de rest.

**Bouwen (na mijn keuze), volgens besluit 16:**
- het kopiepad van stap 2 zonder effecten blijft onaangeroerd; de effecten in een tweede compute-pipeline (uitpakken, mengen, eigen bilineaire filtering), op de GFX-wachtrij, één submit en één fence per beeld;
- schaduw: een masker per venster, één keer vervaagd bij aanmaken en vergroten, bewaard; de omvang uit `GetShadowExtent()`; damage aan de schaduwkant groter;
- glas: Dual Kawase (startwaarde twee niveaus), een bewaard resultaat per glasvlak, alleen opnieuw bij damage eronder of als het glasvenster beweegt; de blur alleen voor (damage plus blurstraal) ∩ glasgebied; het glasgebied uit `GetGlassRegion()` van de R5-decorator plus de glas-views;
- de calls precies zoals in het ontwerp: `<WindowEffects.h>`, namespace `BAppearanceKit`, `window_effects()`, `set_window_effects()`, `set_window_glass()`, `set_view_glass()`, de vlaggen `B_WINDOW_SHADOW` en `B_WINDOW_FROSTED_GLASS` (en `B_WINDOW_ANIMATIONS` gereserveerd voor 4b). Via een eigen `AppServerLink`, nieuwe codes aan het eind van `ServerProtocol.h`. Niets in `libbe`, geen `B_TRANSPARENT_COLOR`. app_server bewaart de twee waarden in zijn `appearance`-bestand; een oud bestand zonder de sleutels betekent beide aan;
- het vak "Window effects" met "Shadow" en "Frosted glass" op Look and feel van Appearance, in Settings én in het losse Appearance-programma. Een wijziging werkt meteen; Defaults zet beide aan; Revert zet terug wat er was toen de pagina openging. De Settings-indeling van `AGENTS.md` blijft. Logregels `app_server: shadow on/off`, `frosted on/off`;
- in de CPU-modus worden de calls en vinkjes geaccepteerd en bewaard, zonder zichtbaar effect;
- elke call in `docs/LIBR5.md` en `docs/APPEARANCE-KIT.md` met een kort voorbeeld, in dezelfde commit.

**Testen, eerlijk:**
- QEMU op Lavapipe: met beide vinkjes uit `compose compare ok` en nooit `MISMATCH`; beide vinkjes aan en uit met de logregels; Revert en Defaults; een venster slepen over glas; een programma zonder glas-call ziet er hetzelfde uit; schermafbeeldingen in het rapport.
- Metingen in `CHANGELOG.md`, Lavapipe 1080p: compose-tijd zonder effecten (kleine damage en slepen), alleen schaduw (slepen), alleen glas (tab en kader), en glas over het hele scherm als slechtste geval. Haalt dat laatste geen 16 ms: zeg het, niet zelf een lager niveau kiezen.
- RADV op de 5625U is "Onbewezen" tot de stick.

Eigen tak `claude/m11-effects` vanaf `origin/main`. Vraag vóór je jamt, pusht en een PR opent. Meld kort: Veranderd / Getest / Onbewezen / Documentatie, en stop.

---

## Stap 4b

Voer stap 4b uit van `generated/gpu-m11/BRIEF.md`: vensteranimaties. Alleen stap 4b. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan besluit 8 en 16 in de brief en `generated/gpu-m11/ANIMATIONS.md`. Je eerste antwoord: de regels die gelden en het plan. Wacht op mijn "ga".

Openen en sluiten (vervagen en schalen) en het wisselen van werkruimte (schuiven), alleen in de compositor, op de effecten-pipeline van stap 4. Het vinkje "Animations" in het vak "Window effects" is de vlag `B_WINDOW_ANIMATIONS` van `set_window_effects()`; geen nieuwe call. Bijwerken in `docs/LIBR5.md` en `docs/APPEARANCE-KIT.md`. Niet: verbergen en terugzetten naar de Deskbar, iets in de Deskbar of in programma's. Een animatie houdt nooit invoer op; vergelijken alleen op een stilstaand beeld. De lengtes zijn startwaarden; maak eerst een korte opname of reeks schermafbeeldingen van open en dicht en stop, ik kijk mee. Vraag vóór je jamt. Test en passregels: de brief, stap 4b. Meld kort en stop.

---

## Stap 5

Voer stap 5 uit van `generated/gpu-m11/BRIEF.md`: de pauze voor direct windows en de geforceerde fout. Alleen stap 5. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan `docs/APP-SERVER-COMPOSITOR.md`, "The direct-window pause" en "The switch to the CPU mode". Je eerste antwoord: de regels die gelden, het plan, en de volgorde van de locks zoals jij die in de bron vindt. Wacht op mijn "ga".

Een klein `BDirectWindow`-testprogramma (niet in het image, tenzij de stick het nodig heeft) en `gpu_compositor_fail true`. Vraag vóór je jamt. Test: VirtualBox of QEMU, de passregels uit de brief, stap 5. Daarna: vraag of je mag pushen en een PR mag openen voor deze stap. Meld kort en stop.
