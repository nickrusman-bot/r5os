Drie korte prompts. Geef ze één voor één, elk pas na het rapport van de vorige. Dezelfde tak `claude/m11-compositor`, één commit per stap.

---

## Stap 3a (alleen papier)

Voer stap 3a uit van `generated/gpu-m11/BRIEF.md`: het ontwerp voor vensters zonder kopie. Alleen documentatie: geen code, geen jam. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan besluit 15 in de brief, en `docs/APP-SERVER-COMPOSITOR.md`, "Zero-copy windows" en "Decided after step 0", punt 3. Je eerste antwoord: de regels die gelden en je plan. Wacht op mijn "ga".

Werk besluit 15 uit zoals stap 3a in de tabel zegt. Kijk in de echte bron, niet uit je geheugen: de R5-interface (`r5_gpu.h`, de kernel, de R5 DRM-bibliotheek), Mesa (hoe RADV een fd importeert, en `wsi_common_haiku.cpp`) en de Haiku-boom in WSL (veilige willekeur in de kernel, de afzender van een port-bericht). Past de stap van token naar lokale fd niet zonder een RADV-wijziging: stop en vraag. Waar de bron iets anders zegt dan besluit 15: zet het onder "Open questions", niet zelf kiezen.

Eén documentatie-PR vanaf `origin/main`, tak `claude/m11-zero-copy-design`, alleen na mijn ja. Meld kort en stop.

---

## Stap 3b

Voer stap 3b uit van `generated/gpu-m11/BRIEF.md`: vensters zonder kopie (A2), precies zoals het ontwerp van 3a dat ik heb goedgekeurd. Alleen stap 3b. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan het 3a-ontwerp. Je eerste antwoord: de regels die gelden en het plan. Wacht op mijn "ga".

Dit raakt de kernel en de R5 DRM-bibliotheek: bekijk het pad en de foutgevallen. Geen call en geen nummer dat niet in het 3a-ontwerp staat. Wijzigt de WSI, dan een nieuw `mesa_radv`-pakket met de pin en de libdrm-symboolcheck. Vóór de jam: SMAP-review van de diff en de QEMU-SMAP-boot. De tests uit de tabel (stap 3b), elk met een pass- en een failregel. Vraag vóór je jamt. Meld kort en stop.

---

## Stap 4

Voer stap 4 uit van `generated/gpu-m11/BRIEF.md`: de schaduw (A3), het frosted glas (A4) en de twee vinkjes. Alleen stap 4. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw. Dit is ook UI-werk: lees de alinea onder LibR5 in `AGENTS.md` en volg die. Dan besluit 16 in de brief, en `docs/APP-SERVER-COMPOSITOR.md`, "The compose pass", "What programs and developers see" en "Settings". Je eerste antwoord: de regels die gelden en het plan. Wacht op mijn "ga".

Volg besluit 16: het kopiepad zonder effecten blijft onaangeroerd, de effecten in een tweede compute-pipeline, Dual Kawase met een bewaard blur-resultaat, de blur alleen waar damage een glasvlak raakt, de GFX-wachtrij.

Eerst het uiterlijk: maak twee of drie varianten van schaduw (grootte, zachtheid) en glas (blurniveau, doorzichtigheid van de tab), elk als schermafbeelding in Lavapipe van hetzelfde bureaublad, actief en inactief venster. Leg ze naast elkaar met de waarden erbij en stop. Ik kies; pas daarna maak je de rest af.

Meten (in `CHANGELOG.md`), op Lavapipe, 1080p: compose-tijd zonder effecten (kleine damage en een venster slepen), alleen schaduw (slepen), alleen glas (titelbalk en kader) en glas over het hele scherm als slechtste geval. Haalt dat laatste geen 16 ms, zeg het; niet zelf een lager niveau kiezen.

De calls heten zoals in het ontwerp (`<WindowEffects.h>`, `BAppearanceKit`); bedenk er geen andere. Niets in `libbe`, geen `B_TRANSPARENT_COLOR`. Het vak "Window effects" op Look and feel van Appearance, in Settings én in het losse Appearance-programma. De Settings-indeling van `AGENTS.md` blijft. Elke call in `docs/LIBR5.md` en `docs/APPEARANCE-KIT.md` in dezelfde commit. Vraag vóór je jamt. Test: VirtualBox of QEMU met Lavapipe, het M11-scenario 60 s, met en zonder effecten; de meetregels in `CHANGELOG.md`. Meld kort en stop.

---

## Stap 5

Voer stap 5 uit van `generated/gpu-m11/BRIEF.md`: de pauze voor direct windows en de geforceerde fout. Alleen stap 5. Lees eerst `AGENTS.md` en de "Rules for the agent" opnieuw, dan `docs/APP-SERVER-COMPOSITOR.md`, "The direct-window pause" en "The switch to the CPU mode". Je eerste antwoord: de regels die gelden, het plan, en de volgorde van de locks zoals jij die in de bron vindt. Wacht op mijn "ga".

Een klein `BDirectWindow`-testprogramma (niet in het image, tenzij de stick het nodig heeft) en `gpu_compositor_fail true`. Vraag vóór je jamt. Test: VirtualBox of QEMU, de passregels uit de brief, stap 5. Daarna: vraag of je mag pushen en een PR mag openen voor stap 2 tot 5 (titel bijvoorbeeld "Graphics - Windows are composed on the GPU"). Meld kort en stop.
