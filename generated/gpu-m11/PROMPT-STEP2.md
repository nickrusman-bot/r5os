Voer stap 2 uit van `generated/gpu-m11/BRIEF.md`: de kern van de compositor (A1). Alleen stap 2.

Eerst, voordat je iets verandert:
1. Lees `AGENTS.md` helemaal. Dan de brief, vooral "Rules for the agent", besluiten 1, 4, 5, 7, 11 en 14, en stap 2 in de tabel. Dan `docs/APP-SERVER-COMPOSITOR.md` helemaal, en `docs/APP-SERVER-GPU.md` (de M8-kopie, die hier na de compose draait).
2. Controleer dat stap 1 (het R5-kader, de hooks, de ABI-versie) in `main` zit. Zo niet: stop en meld het.
3. Je eerste antwoord: welke regels uit `AGENTS.md` gelden (hooguit vijf regels), het plan in een paar regels, en waar je de bron anders vindt dan het ontwerp. Daarna wacht je op mijn "ga".

Wat (volg het ontwerp, bedenk geen tweede weg):
- de compositor als laag in app_server, achter `gpu_compositor true` in de radeon_hd-instellingen; lees die zoals de M8-kopie `gpu_app_server` leest;
- RADV als `R5_GPU_CAP_AVAILABLE` gezet is, anders Lavapipe; lukt geen van beide, dan de CPU-modus;
- één buffer per venster (de levenscyclus uit het ontwerp), `Window::DrawableRegion()`, `SetRendererOffset`, geen herteken van onderliggende vensters bij verplaatsen;
- compose alleen bij damage, hooguit één keer per verversperiode uit de `display_mode`;
- RADV: naar de scanout met de M8-`DMA_DATA`-kopie na de fence. Lavapipe: `_CopyToFront`;
- besluit 14: een te late fence is geen fout (`compose late <n> ms`); terug naar de CPU-modus alleen bij een mislukte submit of een verloren context; één herstart na een geslaagde reset;
- de vergelijkmodus en de meetregels met `gpu_debug true`, precies de regels uit het ontwerp;
- de shaders in GLSL, met een host-`glslangValidator` omgezet in een gegenereerde header die nooit met de hand wordt bewerkt. Glslang is een bouwtool: controleer eerst de SPDX-koppen van de vastgezette versie en zet het in `THIRD_PARTY_NOTICES.md` en `docs/LICENSING.md`. Vind je een GPL-bestand in wat gebouwd wordt: stop en vraag.

Niet: zero-copy, schaduw, glas, de vinkjes, de pauze voor direct windows (stap 3 tot 5). Geen kernelwijziging. Een programma dat alleen `libbe` gebruikt, merkt niets.

Tak `claude/m11-compositor` vanaf `origin/main`. Vraag het vóór je jamt. (Gedaan en gemerged als PR #4247, 13 nov.)

Test: VirtualBox of QEMU met `gpu_compositor true` (Lavapipe), plus Pulse, ActivityMonitor en een klok half onder een ander venster, en dat venster weg. Passregels uit de brief, stap 2. Ook één boot zonder de instelling: dan geen regel met `compositor`. Laat een korte kaart achter.

`CHANGELOG.md`: de verandering voor half bedekte vensters ("Decided after step 0", punt 1) en wat je meet.

Meld kort: Veranderd / Getest / Onbewezen / Documentatie, en stop. Ik betaal per token.
