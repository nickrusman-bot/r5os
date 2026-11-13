# Prompt voor Gemini 4: GPU-buffers veilig tussen programma's delen

Kopieer alles onder de streep naar Gemini. Het is bewust neutraal: er staat geen voorkeur in.

---

Je bent een ervaren reviewer van grafische stacks en OS-beveiliging (DRM/GEM, dma-buf, Wayland, compositors, capability-ontwerp). Ik vraag één ontwerp, met een duidelijk advies en de reden in gewone taal. Ik ben producteigenaar, geen kernelontwikkelaar: geef opties, een aanbeveling en waarom.

## Context

R5 Operating System is een onafhankelijk besturingssysteem op basis van Haiku (de opvolger van BeOS). De displayserver `app_server` stelt het scherm samen met Vulkan. Een Vulkan-programma (bijvoorbeeld een spel) tekent in zijn eigen swapchain-beelden. Nu worden die beelden via de CPU gekopieerd naar een `BBitmap` die het venster tekent. Dat willen we vervangen: app_server moet de GPU-beelden van het programma rechtstreeks lezen, zonder kopie.

Feiten:
- **Kernel:** geen Linux, geen `amdgpu`-port. De driver `radeon_hd` heeft een kleine eigen R5-GPU-interface: ioctls op een render-node `/dev/gpu/radeon_hd/<n>`. Elke open van die node is een "file" met een eigen GPU-adresruimte (een eigen VMID per file). Buffers hebben per file een handle (een `uint32`, zoals GEM-handles). Sync-objecten hebben ook per-file handles.
- **Al in de interface, nog niet werkend:** `R5_GPU_BO_EXPORT` en `R5_GPU_BO_IMPORT` (geven nu `B_UNSUPPORTED`). De nummers liggen vast; de structuren erachter mogen we nog ontwerpen. De interface is additief: wat eenmaal geleverd is, verandert niet. Een nieuwe call is mogelijk, maar alleen als het echt nodig is, leverancier-neutraal (later komt Intel erbij).
- **De kernel kent de aanroeper:** hij kan de team-informatie lezen (een team is een proces in Haiku). Zo is er al één call die alleen app_server mag doen: de kernel controleert het pad van het uitvoerbare bestand van het aanroepende team.
- **Haiku heeft geen dma-buf.** Wat Haiku wel heeft (controleer dit zelf, ik weet het niet zeker):
  - areas: gedeeld geheugen met een id, dat een ander team kan klonen, met een vlag `B_CLONEABLE_AREA`;
  - ports: berichtenwachtrijen, waarbij de ontvanger het team van de afzender kan opvragen;
  - `BMessage` over ports, wat app_server al gebruikt voor alle communicatie met programma's;
  - Unix-domain sockets, mogelijk met het doorgeven van file descriptors (`SCM_RIGHTS`).
- **Vulkan:** Mesa 25.3.6 RADV via een eigen "R5 DRM-bibliotheek" die libdrm-calls vertaalt naar de R5-interface. PRIME (`DRM_IOCTL_PRIME_HANDLE_TO_FD` / `FD_TO_HANDLE`) en het delen van sync-objecten (`SYNCOBJ_HANDLE_TO_FD` / `FD_TO_HANDLE`) worden daar nu als onbekend gelogd. De window-system-laag van Mesa (WSI) voor R5 is eigen code (`VK_RFIVE_view_surface`), MIT.
- **Gebruikers:** één beheerder en gewone gebruikers met eigen uid's. app_server en de kernel draaien als uid 0; programma's als de aangemelde gebruiker. Een programma mag nooit de beelden of buffers van een ander programma lezen of overschrijven. Een eerdere test bevestigt dat de GPU-adresruimtes per file gescheiden zijn.
- **Ook nodig in dezelfde stap:** app_server wil de framebuffer die op het scherm staat (de scanout-buffer, die de kernel al aan app_server geeft) in Vulkan importeren.
- **Sync:** het programma tekent, app_server moet wachten tot het klaar is, en het programma mag het beeld pas hergebruiken als app_server klaar is met lezen. Er is één GFX-ring. Er is een GPU-reset die alle contexten van de getroffen files als verloren markeert.

## Wat ik van je wil

1. **Je advies:** hoe ziet de handle eruit die van het programma naar app_server gaat, en hoe gaat die erheen? Geef twee of drie opties, bijvoorbeeld:
   - een geheime, niet te raden token die de kernel uitgeeft;
   - een file descriptor;
   - een expliciete toestemming voor één doel-team.

   Per optie: hoe het werkt, wat de kernel bijhoudt, en waarom je er wel of niet voor kiest. Maximaal een pagina.
2. **Dreigingen**, en hoe je advies ze tegenhoudt:
   - een programma dat handles raadt;
   - een programma dat een handle doorgeeft aan een derde;
   - een programma dat een buffer vrijgeeft terwijl app_server hem nog leest;
   - een programma dat liegt over de grootte of het formaat;
   - een programma dat de sync nooit laat afgaan;
   - een programma dat crasht;
   - hergebruik van een handle na vrijgeven;
   - een programma dat zich voordoet als app_server.
3. **Levensduur en opruimen:** wie houdt een buffer in leven, wat gebeurt er als een van beide teams stopt, en wat er bij een GPU-reset gebeurt.
4. **Sync-objecten:** dezelfde vraag voor het delen van een sync-object, of een alternatief (bijvoorbeeld een timeline-semaphore). Moet dat dezelfde handle-vorm hebben?
5. **De vertaling naar Mesa:** past je ontwerp op PRIME zoals RADV dat verwacht (een fd), of moet de R5 DRM-bibliotheek iets nabootsen? Wat is het minste werk zonder het veilig delen op te geven?
6. **Voorbeelden uit andere systemen** (Linux dma-buf en `memfd`, Fuchsia VMO-handles en sysmem, Windows NT-handles voor gedeelde D3D-resources, macOS IOSurface, Android `AHardwareBuffer`), kort: wat nemen we over, wat niet.
7. **Testen:** een korte lijst concrete tests (host of in een VM) die bewijzen dat een ander programma een buffer niet kan importeren.
8. **Wat je niet zeker weet:** zeg het. Verzin geen Haiku-API's. Noem het Haiku-bestand of de functie die ik moet nakijken.
