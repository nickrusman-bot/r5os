Besluit 15b staat in `generated/gpu-m11/BRIEF.md` (13 nov, na twee rondes second opinion en nagekeken in de bron). Pas je 3a-ontwerp daarop aan; waar 15b iets anders zegt dan 15, geldt 15b.

Kort:
1. Grootte bij import: een vijfde libdrm-patch in `amdgpu_bo_import`, geen `shm_open`, geen RADV-patch.
2. Willekeur: per export uit de random bus manager, geen eigen crypto.
3. Geen gedeelde sync-objecten. Bij present legt de kernel op de buffer het ringnummer van de laatste submit van dat bestand vast (één nieuwe additieve call); app_server vraagt met timeout 0. "Weer vrij" via een linkbericht.
4. Een nieuw linkbericht naast `SetViewBitmap`; de kernel pint het exporterende team, app_server geeft `ClientTeam()` mee bij de import.
5. `GEM_OP` en `GEM_METADATA` geven echte grootte en domein en neutrale lineaire waarden; gedeelde beelden zijn lineair.
6. Token 5 s, 32 exports per bestand. `kMaxBo` en `kMaxClientBo` zijn allebei 256: in 3b omhoog en per programma ruim lager, met waarden uit een meting.
7. Na een reset: de vensterachtergrond tot een nieuw beeld.

Kijk in de WSL-boom na en zet het in het ontwerp:
- doet RADV zelf een `lseek` in `radv_amdgpu_winsys_bo_from_fd`?
- hoe reageert RADV op `ENOSYS` van `GEM_METADATA` en `GEM_OP`?
- welke functie biedt de random bus manager aan andere kernelcode?
- hoeveel buffers maakt RADV voor VulkanTeapot?

Past iets hiervan niet in de bron: zet het onder "Open questions" en stop.
