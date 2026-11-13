Besluit over de vensterbuffers (13 nov, met een second opinion): ze blijven via Vulkan aangemaakt, zoals jij het gebouwd hebt. Geen `R5_GPU_BO_CREATE` door app_server zelf. Zie besluit 5 in `generated/gpu-m11/BRIEF.md` (bijgewerkt).

Graag in deze stap, verder niets:
1. Eén logregel bij de eerste vensterbuffer: `app_server: window memory type <i> flags <hex>`.
2. Sluit `DEVICE_LOCAL` niet uit in de keuze van het geheugentype; de huidige keuze (eerst host-cached) blijft.
3. In `docs/APP-SERVER-COMPOSITOR.md`, "Decided in step 2": schrijf dat dit een vaste keuze is, geen tijdelijke terugval, en dat de tabel "Window buffer life cycle" daarop aangepast wordt. Haal daar ook "no retry" weg onder "During a session", want besluit 14 doet één herstart.
4. Noem in je rapport dat het vrijgeven van de buffers van verborgen vensters nog niet gebouwd is.

5. Eén logregel per Vulkan-wachtrijfamilie bij het opstarten: `app_server: vulkan queue family <i> flags <hex>`. Kies een familie met de graphics-bit (niet een familie met alleen compute): de R5 DRM-bibliotheek weigert elke opdracht die niet voor de GFX-ring is (`r5_translate.c`, de IB-check). Zie besluit 16 in de brief.

De schrijftest van Painter en het budget via `VK_EXT_memory_budget` komen later (de stick); niet nu bouwen.
