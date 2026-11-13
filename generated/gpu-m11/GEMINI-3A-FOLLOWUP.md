# Prompt voor Gemini 4: vervolg op de open vragen van 3a, nagekeken in de bron

Kopieer alles onder de streep naar Gemini, in hetzelfde gesprek als de vorige vraag.

---

Dank je. Een tweede reviewer stelt op drie punten iets anders voor. Kijk deze voorstellen na **in de bron van deze repository**, niet uit je geheugen, en zeg per punt of je het voorstel steunt, met het bestand en de regel waarop je je baseert. Waar je de bron niet kunt bereiken (Mesa en de Haiku-boom staan grotendeels buiten deze map, op de WSL-bouwmachine), zeg je dat en vertel je wat ik daar moet opzoeken.

1. **De grootte bij een import.** Je stelde voor de grootte af te vangen in `distro/graphics/libdrm-r5/r5_translate.c`. Het tegenvoorstel: dat kan niet, omdat `lseek` een libc-aanroep is en geen ioctl, dus de vertaallaag ziet hem niet. R5 patcht libdrm wel al zelf (`distro/graphics/libdrm-r5/patches/`, ingelezen door `haiku.py`). Een vijfde kleine patch in libdrm's `amdgpu_bo_import` vraagt de grootte dan aan de R5-bibliotheek in plaats van `lseek(fd, 0, SEEK_END)` te doen. Geen `shm_open`, geen patch in RADV. Klopt dit? Doet RADV zelf ook nog een `lseek` op de fd, buiten libdrm om?
2. **Willekeur.** Je stelde een eigen CSPRNG (ChaCha20) in de driver voor, gevoed met één seed. Het tegenvoorstel: geen eigen cryptografie in de kernel. Exports zijn zeldzaam (een paar per swapchain, niet per beeld), dus per export rechtstreeks lezen uit Haiku's random bus manager is snel genoeg. Welke functie biedt die module aan andere kernelcode aan (bestand en naam)? Is lezen per export veilig en snel genoeg?
3. **Synchronisatie.** Het tegenvoorstel vermijdt gedeelde sync-objecten tussen programma's:
   - Er is één GFX-ring. De kernel onthoudt per buffer het ringnummer van de laatste opdracht die erin schrijft.
   - **"Klaar":** app_server vraagt de kernel met timeout 0 of de buffer klaar is (zoals `DRM_AMDGPU_GEM_WAIT_IDLE`). Is het beeld niet klaar, dan toont hij het vorige.
   - **"Weer vrij":** app_server stuurt na zijn eigen compose-fence een linkbericht terug; de WSI wacht daarop.

   Het argument tegen optie (a): als de compose-opdracht op de GPU op een sync-object van het programma wacht, kan een vastgelopen programma het hele bureaublad stilzetten, tenzij je vóór elke submit controleert. Steun je dit tegenvoorstel? En vooral: kent de R5-kernel per opdracht al de buffers die erin zitten (de bufferlijst die RADV meegeeft)? Kijk in `distro/graphics/libdrm-r5/r5_translate.c` (de CS-chunks) en de kerneldriver onder `distro/graphics/src/add-ons/kernel/drivers/graphics/r5gpu/`.

Kijk daarnaast in de bron na:
4. **`GEM_METADATA` en `GEM_OP`:** waar in `r5_translate.c` worden onbekende ioctls afgehandeld, en wat krijgt RADV dan terug? Stopt een import daardoor, of gaat hij door?
5. **`kMaxBo`:** waar staat die grens in de kerneldriver? Geldt hij voor het hele systeem of per bestand? Hoeveel buffers maakt RADV ongeveer per programma (VulkanTeapot is het testprogramma)?

Antwoord kort: per punt "steun / steun niet / kan niet nakijken", de bron (bestand:regel), en één zin waarom.
