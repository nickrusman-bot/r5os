# Prompt voor Gemini 4: de open vragen van het zero-copy-ontwerp (stap 3a)

Kopieer alles onder de streep naar Gemini. Het is bewust neutraal: de voorstellen van de ontwerper staan erbij als voorstel, niet als besluit.

---

Je bent een ervaren reviewer van grafische stacks en OS-beveiliging (DRM/GEM, dma-buf, libdrm, Mesa RADV, compositors). Je hebt eerder geadviseerd over dit ontwerp. Nu liggen er zes open vragen. Per vraag wil ik een advies, met de reden in gewone taal. Ik ben producteigenaar, geen kernelontwikkelaar.

## Context

R5 Operating System is een onafhankelijk besturingssysteem op basis van Haiku. De displayserver `app_server` stelt het scherm samen met Vulkan (RADV uit Mesa 25.3.6, of Lavapipe). Een Vulkan-programma moet zijn swapchain-beelden aan app_server geven zonder kopie via de CPU.

Wat al besloten is:
- **Een token van de kernel:** `R5_GPU_BO_EXPORT` maakt een niet te raden token van minstens 128 bits. Alleen het echte `/system/servers/app_server` mag het importeren (`R5_GPU_BO_IMPORT`). Het token werkt één keer, verloopt na korte tijd, en per bestand mag maar een beperkt aantal exports openstaan.
- **Referentieteller:** de kernel houdt de buffer in leven met een referentieteller. app_server neemt de echte grootte van de kernel over, niet die van het programma.
- **Synchronisatie:** twee sync-objecten die elk één kant op werken, per swapchain-beeld. De compositor wacht nooit op een programma, maar toont dan het vorige beeld.
- **Geen patch in RADV:** aan de kant van app_server maakt de R5 DRM-bibliotheek (die libdrm-calls vertaalt naar de R5-interface) van een token een lokale file descriptor. RADV ziet dan het gewone `VK_KHR_external_memory_fd`.
- **Na een GPU-reset** herstart de compositor één keer.

Feiten uit de bron, gevonden bij het uitwerken:
- Sync-objecten leven nu binnen het bestand (de open render-node) van één programma.
- De WSI van R5 (`VK_RFIVE_view_surface`, eigen MIT-code in Mesa) geeft beelden aan app_server met `SetViewBitmap`, een intern linkbericht van Haiku's `libbe`, geen `BMessage`. app_server kent per verbinding het team van het programma (`ClientTeam()`).
- In de kernel is Haiku's `secure_random_value()` een hash van tijd en tellers. De echte generator zit achter `/dev/random`, in de random bus manager.
- De kernel heeft een systeemgrens `kMaxBo = 256` buffers; elke import kost een eigen plek.
- De R5 DRM-bibliotheek kent `GEM_METADATA` en `GEM_OP` niet. Volgens de ontwerper heeft RADV die bij elke import nodig.

## De vragen

1. **Welk soort fd.** libdrm leest de grootte van een geïmporteerde buffer met `lseek(fd, 0, SEEK_END)` op de fd zelf, en in Haiku heeft een apparaatbestand grootte 0. Voorstel van de ontwerper: een naamloos `shm_open`-bestand van de juiste grootte als drager, zodat alles in de bibliotheek blijft. Is dat verstandig, zijn er betere opties (bijvoorbeeld de grootte apart doorgeven, of de libdrm-call in de bibliotheek afvangen), en wat zijn de risico's (lekken van fd's, hergebruik, een andere partij die dezelfde naam opent)?
2. **Willekeur voor het token.** Is een hash van tijd en tellers voldoende voor een token dat alleen app_server mag gebruiken, gegeven dat de kernel daarnaast het doel controleert? Of moet het uit de echte generator komen? Voorstel van de ontwerper: een leesoproep toevoegen in een R5-kopie van de random bus manager. Wat is het kleinste veilige pad?
3. **Sync delen.** Sync-objecten tussen programma's delen is een grotere kernelwijziging dan verwacht. Alternatief van de ontwerper: reeksnummers in het bericht in plaats van gedeelde sync-objecten. Vergelijk:
   - (a) sync-objecten delen met hetzelfde tokenmechanisme;
   - (b) reeksnummers met wachten op de fence van het programma in de kernel;
   - (c) een timeline die de kernel bezit.

   Kijk naar veiligheid, de garantie dat de compositor nooit wacht, de hoeveelheid werk, en of het past bij wat RADV en de WSI verwachten.
4. **Transport en afzender.** Er is geen `BMessage`; de beelden gaan via `SetViewBitmap`, een linkbericht. Twee vragen:
   - hoe bereiken de tokens app_server: een nieuw linkbericht, of uitbreiding van het bestaande?
   - hoe weet app_server zeker van welk programma een token komt? Voorstel: de kernel geeft bij de import het exporterende team terug, en app_server vergelijkt dat met `ClientTeam()` van de verbinding. Dekt dat het misbruik waarbij programma B een token van programma A doorstuurt? Is er een sterkere of eenvoudigere manier?
5. **Getallen.** Voorgestelde startwaarden:
   - een token leeft 2 s;
   - hoogstens 16 openstaande exports per bestand;
   - de systeemgrens `kMaxBo = 256` blijft, en elke import kost een eigen plek.

   Zijn die waarden redelijk voor swapchains van 2 tot 4 beelden, meerdere vensters en het opnieuw aanmaken bij een resize? Moet de systeemgrens omhoog of per team verdeeld worden, zodat één programma niet alle plekken kan opmaken?
6. **Het laatste beeld na een reset.** Na een herstart van de compositor zijn de geïmporteerde buffers weg, dus het "laatste frame" van een Vulkan-venster bestaat niet meer. Wat toon je tot het programma een nieuw beeld geeft:
   - een lege of grijze vlakte;
   - een kopie via de CPU die je vóór de reset bewaarde;
   - het programma om een nieuw beeld vragen.

   Wat doen andere compositors?

Daarnaast:
7. **`GEM_METADATA` en `GEM_OP` ontbreken.** Klopt het dat RADV die bij een import nodig heeft (noem het bestand en de functie in Mesa)? Wat moet de R5 DRM-bibliotheek minimaal teruggeven om RADV tevreden te houden, zonder metadata tussen programma's te delen die niet gedeeld mag worden (zoals tiling)?
8. **Wat je niet zeker weet:** zeg het. Verzin geen API's van Haiku of Mesa. Noem het bestand of de functie die ik moet nakijken.

Geef per vraag hoogstens een korte alinea, en sluit af met een lijst "doen", "niet doen" en "eerst nakijken".
