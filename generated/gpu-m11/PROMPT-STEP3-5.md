Korte prompts. Geef ze één voor één, elk pas na het rapport van de vorige. Stap 2 is gemerged (PR #4247, 13 nov). Elke stap krijgt een eigen tak vanaf `origin/main` en een eigen PR, pas nadat de vorige stap gemerged is; niet gestapeld.

---

## Stap 3a (alleen papier)

Voer stap 3a uit van `generated/gpu-m11/BRIEF.md`: het ontwerp voor vensters zonder kopie. Alleen documentatie: geen code, geen jam. Stap 2 (de compositor-kern, PR #4247) zit al in `main`: ontwerp op wat daar echt gebouwd is (`distro/app_server/Compositor.cpp`, `docs/APP-SERVER-COMPOSITOR.md`, "Decided in step 2"), niet op het oude ontwerp. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan besluit 15 in de brief, en `docs/APP-SERVER-COMPOSITOR.md`, "Zero-copy windows" en "Decided after step 0", punt 3. Je eerste antwoord: de regels die gelden en je plan. Wacht op mijn "ga".

Werk besluit 15 uit zoals stap 3a in de tabel zegt. Kijk in de echte bron, niet uit je geheugen: de R5-interface (`r5_gpu.h`, de kernel, de R5 DRM-bibliotheek), Mesa (hoe RADV een fd importeert, en `wsi_common_haiku.cpp`) en de Haiku-boom in WSL (veilige willekeur in de kernel, de afzender van een port-bericht). Past de stap van token naar lokale fd niet zonder een RADV-wijziging: stop en vraag. Waar de bron iets anders zegt dan besluit 15: zet het onder "Open questions", niet zelf kiezen.

Eén documentatie-PR vanaf `origin/main`, tak `claude/m11-zero-copy-design`, alleen na mijn ja. Meld kort en stop.

---

## Stap 3b

Voer stap 3b uit van `generated/gpu-m11/BRIEF.md`: Vulkan-vensters zonder kopie (A2). Alleen 3b; de scanout in Vulkan is 3c. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan besluit 15 en 15b in de brief en in `docs/APP-SERVER-COMPOSITOR.md` het hele 3a-ontwerp (gemerged in #4251 en #4252), behalve "The scanout import into Vulkan". Je eerste antwoord: de regels die gelden, het plan per laag, en wat je niet in QEMU kunt testen. Wacht op mijn "ga".

Bouw precies het ontwerp, geen call of nummer dat er niet in staat:
- kernel: `BO_EXPORT` / `BO_IMPORT` (`+ 17`, `+ 18`), het exportnummer met de rechtencontrole (team vastgelegd bij export, alleen app_server, één keer, 5 s, 32 per bestand), de splitsing van plek en geheugen met de teller, `BO_PRESENT_MARK` (`+ 32`), de grenzen (1024, 512 per bestand, 64 als reserve voor app_server) met de logregel bij een volle grens;
- R5 DRM-bibliotheek: PRIME via een lokale fd, `GEM_OP` en `GEM_METADATA`; de vijfde libdrm-patch voor de grootte; libdrm opnieuw gebouwd;
- WSI: een eigen `AppServerLink` (geen wijziging in libbe), lineaire beelden, een semafoor per beeld, `VK_ERROR_OUT_OF_DATE_KHR` na de timeout; een nieuw `mesa_radv`-pakket met de pin en de libdrm-symboolcheck;
- app_server: de import met de controle van grootte en semafoor tegen `ClientTeam()`, "klaar" met `FENCE_WAIT` timeout 0, "weer vrij" met `release_sem_etc`, en de reset zoals in het ontwerp;
- het testprogramma `r5-share-test` met de pass- en failregels uit het ontwerp.

Testen, eerlijk:
- In QEMU en VirtualBox start `radeon_hd` niet; daar draait alleen het kopiepad op Lavapipe (`zero-copy off: lavapipe`) en moet het bureaublad werken zoals na stap 2.
- De logica van nummers, rechten, verlopen, grenzen en de teller zet je in losse functies met een hosttest, zoals `scripts/test-r5-gpu-checks.cpp` in M10. Die moet slagen.
- `r5-share-test` en het echte pad zonder kopie draaien pas op de 5625U (de stick); schrijf dat als "Onbewezen".

Vóór de jam: SMAP-review van de diff en de QEMU-SMAP-boot. Eigen tak `claude/m11-zero-copy` vanaf `origin/main`. Vraag vóór je jamt, pusht en een PR opent. Meld kort en stop.

## Stap 3c

Voer stap 3c uit van `generated/gpu-m11/BRIEF.md`: de scanout in Vulkan. Alleen 3c, na de merge van 3b. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan "The scanout import into Vulkan" in `docs/APP-SERVER-COMPOSITOR.md`. Je eerste antwoord: de regels die gelden en het plan. Wacht op mijn "ga".

De export van de scanout naar app_server zelf, de import in Vulkan, een `vkCmdCopyBuffer` van de gewijzigde rijen in dezelfde submit, en een nieuwe import na een moduswissel. Het vervangt de CPU-kopie naar de back buffer en de M8-`DMA_DATA`-kopie alleen als het niet slechter meet: zet beide tijden naast elkaar in het rapport, en laat de oude weg staan als de nieuwe slechter is. Ook dit kan alleen op de 5625U; in QEMU blijft het kopiepad. Eigen tak `claude/m11-scanout-import`. Vraag vóór je jamt, pusht en een PR opent. Meld kort en stop.

---

## Stap 4

Voer stap 4 uit van `generated/gpu-m11/BRIEF.md`: de schaduw (A3), het frosted glas (A4) en de twee vinkjes. Alleen stap 4. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw. Dit is ook UI-werk: lees de alinea onder LibR5 in `AGENTS.md` en volg die. Dan besluit 16 in de brief, en `docs/APP-SERVER-COMPOSITOR.md`, "The compose pass", "What programs and developers see" en "Settings". Je eerste antwoord: de regels die gelden en het plan. Wacht op mijn "ga".

Volg besluit 16: het kopiepad zonder effecten blijft onaangeroerd, de effecten in een tweede compute-pipeline, Dual Kawase met een bewaard blur-resultaat, de blur alleen waar damage een glasvlak raakt, de GFX-wachtrij.

Eerst het uiterlijk: maak twee of drie varianten van schaduw (grootte, zachtheid) en glas (blurniveau, doorzichtigheid van de tab), elk als schermafbeelding in Lavapipe van hetzelfde bureaublad, actief en inactief venster. Leg ze naast elkaar met de waarden erbij en stop. Ik kies; pas daarna maak je de rest af.

Meten (in `CHANGELOG.md`), op Lavapipe, 1080p: compose-tijd zonder effecten (kleine damage en een venster slepen), alleen schaduw (slepen), alleen glas (titelbalk en kader) en glas over het hele scherm als slechtste geval. Haalt dat laatste geen 16 ms, zeg het; niet zelf een lager niveau kiezen.

De calls heten zoals in het ontwerp (`<WindowEffects.h>`, `BAppearanceKit`); bedenk er geen andere. Niets in `libbe`, geen `B_TRANSPARENT_COLOR`. Het vak "Window effects" op Look and feel van Appearance, in Settings én in het losse Appearance-programma. De Settings-indeling van `AGENTS.md` blijft. Elke call in `docs/LIBR5.md` en `docs/APPEARANCE-KIT.md` in dezelfde commit. Vraag vóór je jamt. Test: VirtualBox of QEMU met Lavapipe, het M11-scenario 60 s, met en zonder effecten; de meetregels in `CHANGELOG.md`. Meld kort en stop.

---

## Stap 4b

Voer stap 4b uit van `generated/gpu-m11/BRIEF.md`: vensteranimaties. Alleen stap 4b. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan besluit 8 en 16 in de brief en `generated/gpu-m11/ANIMATIONS.md`. Je eerste antwoord: de regels die gelden en het plan. Wacht op mijn "ga".

Openen en sluiten (vervagen en schalen) en het wisselen van werkruimte (schuiven), alleen in de compositor, op de effecten-pipeline van stap 4. Het vinkje "Animations" in het vak "Window effects" is de vlag `B_WINDOW_ANIMATIONS` van `set_window_effects()`; geen nieuwe call. Bijwerken in `docs/LIBR5.md` en `docs/APPEARANCE-KIT.md`. Niet: verbergen en terugzetten naar de Deskbar, iets in de Deskbar of in programma's. Een animatie houdt nooit invoer op; vergelijken alleen op een stilstaand beeld. De lengtes zijn startwaarden; maak eerst een korte opname of reeks schermafbeeldingen van open en dicht en stop, ik kijk mee. Vraag vóór je jamt. Test en passregels: de brief, stap 4b. Meld kort en stop.

---

## Stap 5

Voer stap 5 uit van `generated/gpu-m11/BRIEF.md`: de pauze voor direct windows en de geforceerde fout. Alleen stap 5. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan `docs/APP-SERVER-COMPOSITOR.md`, "The direct-window pause" en "The switch to the CPU mode". Je eerste antwoord: de regels die gelden, het plan, en de volgorde van de locks zoals jij die in de bron vindt. Wacht op mijn "ga".

Een klein `BDirectWindow`-testprogramma (niet in het image, tenzij de stick het nodig heeft) en `gpu_compositor_fail true`. Vraag vóór je jamt. Test: VirtualBox of QEMU, de passregels uit de brief, stap 5. Daarna: vraag of je mag pushen en een PR mag openen voor deze stap. Meld kort en stop.
