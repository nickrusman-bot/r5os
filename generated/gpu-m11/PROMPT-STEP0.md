Voer stap 0 uit van `generated/gpu-m11/BRIEF.md`: het ontwerp op papier voor M11. Lees eerst die brief en `AGENTS.md`. Lees de app_server-bronnen pas als het ontwerp erom vraagt.

Let op: de brief is op 12 nov aangepast. Lees hem opnieuw, ook als je hem al kende. Dit is er veranderd:
- besluit 2: het R5-vensterkader dat ik gekozen heb. De tab en de knoppen blijven zoals in Haiku. Het kader is een lijn van 1 pixel. Er is geen knop om het formaat te veranderen; je sleept aan de rand. In GPU-modus komen er een kleine schaduw en frosted glas bij;
- besluit 8: frosted glas is terug als A4. Shadow en Frosted glass zijn twee losse instellingen;
- besluit 10: een aanknopingspunt voor het glasgebied in de decorator. Ronde hoeken komen pas later, met een tweede decorator;
- besluit 13, nieuw: de BeOS/Haiku-manier voor alles wat een programma of ontwikkelaar ziet;
- besluit 3: nieuwe R5-bestanden zijn MIT.

Alleen documentatie: geen code, geen jam, geen QEMU. De sessie van M9/M10 jamt in WSL; jij blijft daar af.

Lever één PR vanaf `origin/main`, tak `claude/m11-design`:
1. `docs/APP-SERVER-COMPOSITOR.md`, met alles wat de brief bij stap 0 noemt. Let vooral op:
   - het huidige teken- en update-pad, met bestanden, functies en locks uit de echte bron, niet uit je geheugen;
   - de greep om het formaat te veranderen bij een kader van 1 pixel (besluit 2): hoe breed die is en waar hij ligt;
   - besluit 13, de BeOS/Haiku-manier: noem de nieuwe kit-calls in `libr5ui` (Appearance Kit) met hun header. Het gaat om het glas op een venster en om het lezen en zetten van Shadow en Frosted glass. Vergelijk ze met hoe Haiku's Appearance `set_decorator()` en `ui_color` gebruikt. Niets in `libbe`, en geen `B_TRANSPARENT_COLOR`;
   - de twee losse instellingen Shadow en Frosted glass, en waar app_server ze bewaart;
   - het geheugenbudget, de pauze voor direct windows en de overstap naar de CPU-modus;
   - het licentierapport van glslang (project, SPDX, copyright).
2. In `docs/GPU-ARCHITECTURE.md`: "2D in app_server", stap 2 en 3, en de regels voor M11 en M12 in lijn brengen met de brief. Er staat nu nog "shadow and frosted body"; dat wordt de kleine schaduw en frosted glas als losse instellingen.

Waar de bron iets anders zegt dan de brief, schrijf je dat op onder "Open questions". Je kiest dan niet zelf. Andere eigen keuzes komen in de PR-tekst onder "Choices made".

Een PR maak je met `gh pr create` naar `main`. Mergen doe ik, na mijn review.

Meld kort: Veranderd / Getest / Onbewezen / Documentatie. Houd antwoorden kort; ik betaal per token.
