# Prompt voor Gemini 4: hoe de compositor schaduw en glas tekent

Kopieer alles onder de streep naar Gemini. Het is bewust neutraal: er staat geen voorkeur in.

---

Je bent een ervaren ontwikkelaar van compositors en Vulkan-renderers (KWin, Mutter, DWM, wlroots, Picom). Ik vraag één ontwerpkeuze, met een duidelijk advies en de reden in gewone taal. Ik ben producteigenaar, geen GPU-ontwikkelaar: geef opties, een aanbeveling en waarom.

## Context

R5 Operating System is een onafhankelijk besturingssysteem op basis van Haiku (de opvolger van BeOS). De displayserver `app_server` krijgt een compositor op Vulkan. Elk venster heeft een eigen buffer; de compositor stelt daaruit het scherm samen.

Wat er nu gebouwd is (stap 2):
- **Vensterbuffers:** lineaire `VkBuffer`s (geen images), 4 bytes per pixel, in host-visible, gecachet geheugen. De CPU-renderer (Painter, AGG) tekent er continu in, inclusief een alfakanaal.
- **De compose:** één compute-shader. Voor elk rechthoekje van de damage vult hij de achtergrondkleur of kopieert hij de woorden van een vensterbuffer ongewijzigd, van achter naar voren, met een barrier tussen de lagen. Het resultaat is een host-visible buffer ter grootte van het scherm. Daarna wordt dat stuk naar de framebuffer gekopieerd (op de CPU, of met een GPU-DMA-kopie).
- **Alleen op damage,** hooguit één keer per verversperiode. Geen vsync: tearing is in deze fase geen fout.
- **Een vergelijkmodus:** met de effecten uit moet elk pixel exact gelijk zijn aan wat de oude CPU-weg tekent. Dat wordt elke 100 frames gecontroleerd.

Wat in stap 4 komt:
- **Schaduw:** een kleine, korte schaduw onder elk venster, zoals besloten "afgeleid van de randbreedte van de decorator". Het idee in het ontwerp: per venster een alfamasker van het kader, één keer vervaagd bij aanmaken of vergroten/verkleinen, verschoven onder het venster getekend.
- **Frosted glas:** de titeltab, de kaderlijn van 1 pixel en alleen die delen van een venster die een programma zelf als glas aanmeldt. De compositor vervaagt wat eronder al samengesteld is (het idee: verkleinen, blur in twee passes in een compute-shader), en tekent daarover de pixels van het venster met hun alfa.
- **Twee aparte instellingen,** "Shadow" en "Frosted glass", die direct aan en uit gaan.
- **Later (geen deel van deze vraag, wel van invloed):** animaties zoals inzoomen bij openen of krimpen naar de taakbalk, ronde hoeken in een tweede decorator, en in de volgende fase tekent de GPU zelf de vensterinhoud (Skia op Vulkan), waarschijnlijk in `VkImage`s.

Hardware en eisen:
- **Referentie:** AMD Ryzen 5 5625U (Renoir, Vega-iGPU, GC 9.3) met RADV (Mesa 25.3.6). Er is één GFX-ring zonder prioriteit; een zwaar spel op dezelfde GPU kan de compose vertragen. De compute-wachtrij (MEC) draait wel.
- **Software-pad:** zonder ondersteunde GPU draait dezelfde compositor op Lavapipe (Vulkan op de CPU), ook in VirtualBox. Het bureaublad wordt altijd samengesteld, zoals Windows met WARP. Effecten mogen daar uit worden gezet, maar staan standaard aan.
- **Later:** Intel (ANV).
- **Geen leverancierscode in de compositor,** en de shaders zijn GLSL die bij het bouwen naar SPIR-V gaat.

## Wat ik van je wil

1. **Je advies:** blijft de compose een compute-shader, of wordt het een grafische pipeline (een quad per venster met blending), of een mix? En blijven vensterbuffers `VkBuffer`s, of worden ze voor het samplen naar `VkImage`s gekopieerd of als lineaire images gebruikt? Waarom, in gewone taal. Maximaal een halve pagina.
2. **De blur:** welke methode raad je aan voor glas (bijvoorbeeld dual Kawase, Gauss in twee passes, verkleinen en vergroten), met een indicatie van de kosten op een Vega-iGPU en op Lavapipe bij 1080p. Wanneer kun je een blur-resultaat hergebruiken, bijvoorbeeld als er onder het glas niets veranderd is?
3. **Damage en effecten:** hoeveel groter moet de damage worden door de schaduw en de blur, en hoe voorkom je dat een klein veranderd rechthoekje de hele blur opnieuw kost?
4. **Exactheid:** hoe houden we de vergelijking met de effecten uit exact (geen gefilterd samplen, geen afrondingsverschillen), terwijl de effecten wel gefilterd samplen?
5. **Volgorde en sync:** past de compose met effecten in één submit en één fence per frame? Raad je aan de compositor op de compute-wachtrij te zetten zodat een zwaar spel hem minder ophoudt, en wat zijn daarvan de risico's?
6. **Voorbereiding op later:** maakt je keuze animaties (schalen, fade), ronde hoeken en de overstap naar `VkImage`s uit de GPU-renderer makkelijker of moeilijker?
7. **Wat we moeten meten** om je advies te bevestigen, als korte concrete metingen (compose-tijd met en zonder effecten, op RADV en op Lavapipe).
8. **Wat je niet zeker weet:** zeg het. Verzin geen gedrag van Mesa; noem het bestand of de functie die ik moet nakijken.
