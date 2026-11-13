# Prompt voor Gemini 4: hoe app_server zijn vensterbuffers aanmaakt

Kopieer alles onder de streep naar Gemini. Het is bewust neutraal: er staat geen voorkeur in.

---

Je bent een ervaren reviewer van grafische stacks (Vulkan, Mesa/RADV, compositors). Ik vraag één ontwerpkeuze, met een duidelijk advies en de reden in gewone taal. Ik ben producteigenaar, geen GPU-ontwikkelaar: geef opties, een aanbeveling en waarom.

## Context

R5 Operating System is een onafhankelijk besturingssysteem op basis van Haiku (de opvolger van BeOS). We bouwen een compositor in `app_server`, de displayserver. Elk venster krijgt een eigen buffer, en app_server stelt het scherm samen met Vulkan.

Feiten:
- **Hardware:** referentiemachine is een AMD Ryzen 5 5625U (Renoir, GC 9.3, een APU: geen eigen VRAM, alleen een carve-out en systeemgeheugen via GTT). Later komt Intel (ANV) erbij. Zonder ondersteunde GPU draait dezelfde compositor op Lavapipe (Vulkan in software), zoals Windows met WARP.
- **Kernel:** geen port van Linux `amdgpu`, maar een kleine eigen R5-GPU-interface in de driver `radeon_hd`. Relevante calls: `R5_GPU_BO_CREATE` (buffer in GTT of VRAM, met een vlag voor write-combined of gecachet en snooped), `R5_GPU_BO_MAP` (CPU-mapping), `R5_GPU_BO_EXPORT` en `R5_GPU_BO_IMPORT` (buffers tussen processen delen; staan in de interface maar geven nu nog `B_UNSUPPORTED`), `R5_GPU_SCANOUT_BO` (de framebuffer die de driver op het scherm toont, alleen voor app_server). Haiku heeft geen dma-buf; een vorm van gedeelde bufferhandle tussen processen moet nog ontworpen worden.
- **Vulkan:** RADV uit Mesa 25.3.6, via een eigen "R5 DRM-bibliotheek" die de libdrm-amdgpu-calls vertaalt naar de R5-interface. RADV maakt zijn geheugen dus intern aan met `R5_GPU_BO_CREATE`. PRIME- en sync-object-delen werken in die bibliotheek nog niet.
- **Ontwerpregels:** geen leverancierscode in de compositor (dezelfde code op RADV, Lavapipe en later ANV). De R5-GPU-interface is additief: een call die geleverd is, verandert niet.
- **In fase M11** tekent de CPU-renderer (Painter, AGG) de vensterinhoud. Hij schrijft continu in de vensterbuffer via een CPU-pointer. De compositor leest die buffer op de GPU en stelt het scherm samen. Vensters tekenen ook hun bedekte delen. Een venster van 1080p is ongeveer 8 MB (4 bytes per pixel).
- **Later:**
  - stap 3 (M11): Vulkan-programma's tonen zonder kopie. Hun swapchain-beelden gaan van het programma naar app_server met `BO_EXPORT`/`BO_IMPORT` en sync-objecten, en de scanout-buffer wordt in Vulkan geïmporteerd;
  - M12: de GPU tekent zelf de vensterinhoud, via Skia op Vulkan, in plaats van Painter;
  - daarna mogelijk: animaties door de compositor, schermopname en externe weergave.
- **Gemeten in een eerdere fase:** een GPU-kopie die op elke fence wacht, was per kleine kopie duurder dan de CPU-kopie. Wachten op fences per kleine bewerking is duur.

## De keuze

**Optie A: de vensterbuffer via Vulkan aanmaken.** app_server vraagt Vulkan om een buffer (of image) met geheugen dat host-visible, host-coherent en waar mogelijk host-cached is, en mapt dat voor Painter. Op RADV wordt dat onder de motorkap een R5-buffer in GTT. Op Lavapipe is het gewoon systeemgeheugen. Zo is de eerste versie gebouwd.

**Optie B: de vensterbuffer zelf als R5-GPU-buffer aanmaken.** app_server roept `R5_GPU_BO_CREATE` aan (GTT, gecachet, snooped), mapt hem met `R5_GPU_BO_MAP` voor Painter en importeert hem in Vulkan om hem te samplen. Op Lavapipe, zonder render-node, is een ander pad nodig (bijvoorbeeld `VK_EXT_external_memory_host`). Zo stond het in het oorspronkelijke ontwerp.

## Wat ik van je wil

1. **Je advies:** A, B of iets anders, en waarom, in gewone taal. Maximaal een halve pagina.
2. **De risico's van elke optie:**
   - CPU-cache en coherentie: Painter schrijft continu, de GPU leest;
   - hoe RADV op een APU het geheugentype kiest;
   - limieten van GTT en het geheugenbudget;
   - de levensduur van een mapping bij het vergroten of verkleinen van een venster;
   - wat er gebeurt bij een GPU-reset;
   - verschillen tussen RADV, Lavapipe en ANV.
3. **De gevolgen voor later:** stap 3 (zero-copy, scanout-import), M12 (de GPU tekent in de vensterbuffer: moet die dan een `VkImage` zijn in plaats van een `VkBuffer`, en welke optie maakt dat makkelijker?) en het eventueel delen van een vensterbuffer met een ander proces (schermopname).
4. **Wat we moeten meten of controleren** om de keuze te bevestigen, als concrete, korte checks (bijvoorbeeld welk geheugentype RADV teruggeeft, `VK_EXT_memory_budget`, een schrijftest van Painter).
5. **Wat je niet zeker weet:** zeg het. Verzin geen API's of gedrag van Mesa. Als iets afhangt van code die je niet hebt gezien, noem de functie of het bestand in Mesa die ik moet nakijken.
