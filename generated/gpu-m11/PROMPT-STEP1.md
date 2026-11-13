Voer stap 1 uit van `generated/gpu-m11/BRIEF.md`: het R5-vensterkader. Alleen stap 1.

Eerst, voordat je iets verandert:
1. Lees `AGENTS.md` helemaal. Dan de brief (bijgewerkt op 13 nov), vooral "Rules for the agent" bovenaan. Dan `docs/APP-SERVER-COMPOSITOR.md`: "The resize grab area", "Decorator hooks" en "Decided after step 0", punt 2.
2. Controleer dat PR 2 van de after-M10-ronde (Network-pagina's) in `main` zit. Zo niet: stop en meld het.
3. Je eerste antwoord: welke regels uit `AGENTS.md` voor deze stap gelden (hooguit vijf regels), wat je doet en wat je niet doet. Daarna wacht je op mijn "ga".

Wat:
- de R5-decorator, gebaseerd op Haiku's `DefaultDecorator` (besluit 2): tab en knoppen zoals Haiku, een kader van 1 pixel, geen knop voor het formaat, de invoermarge buiten de lijn (`GetInputMargin()`, `Desktop::WindowAt()`);
- de drie hooks aan het eind van `Decorator`, met hun standaard; de ABI-versie per add-on en de controle in `DecorManager` met de regel `app_server: decorator <name> refused: abi <n>, need <m>`;
- Be, Flat, Mac en Win opnieuw gebouwd met de versie, zonder zichtbare wijziging.

Niet: compositor-code, `libr5ui`-calls, schaduw of glas, stap 1b of 1c, iets anders uit de roadmap of andere briefs. Wat je onderweg ziet, zet je onder "Seen, not done".

Tak `claude/m11-decorator` vanaf `origin/main`. Nieuwe bestanden MIT met SPDX; gekopieerde Haiku-bestanden houden hun kop, R5-delen krijgen SPDX-Snippet-tags. Vraag het vóór je jamt, vóór `git push` en vóór een PR. Nooit mergen, nooit naar `main`.

Test: één verse boot in VirtualBox of QEMU (de brief, stap 1). Laat een korte kaart achter met de passregels die ik terug moet plakken. Waar de bron iets anders zegt dan het ontwerp: stop en vraag, niet zelf kiezen.

Meld kort: Veranderd / Getest / Onbewezen / Documentatie, en stop. Ik betaal per token.
