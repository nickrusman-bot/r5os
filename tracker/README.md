# R5 OS bug tracker

Haiku-tickets blijven op [dev.haiku-os.org](https://dev.haiku-os.org/). Hier houden we bij **wat R5 OS daarmee doet**.

| Bestand | Inhoud |
|---|---|
| `haiku-open.csv` | Snapshot van open Haiku-tickets (id, samenvatting, component, …) |
| `r5-status.tsv` | Onze status per ticket-id |
| `STATUS.md` | Gegenereerd overzicht (niet met de hand bewerken) |

Statuswaarden: `open`, `in-progress`, `fixed`, `wontfix`, `not-applicable`.

Importeren overschrijft nooit een bestaande R5-status. Nieuwe Haiku-tickets komen erbij als `open`. Tickets die Haiku heeft gesloten en die bij ons nog `open` zijn, verdwijnen uit `haiku-open.csv` maar blijven in `r5-status.tsv` tot je ze zelf zet.

Trac zit achter een bot-check. Sync in een normale browser, sla CSV op, daarna:

```bash
python3 scripts/tracker-import.py ~/Downloads/query.csv
python3 scripts/tracker-set.py 12345 in-progress --branch r5/fix-foo
python3 scripts/tracker-set.py 12345 fixed --commit abcdef1
python3 scripts/tracker-report.py
```

Elke R5-statuswijziging moet `STATUS.md` opnieuw genereren (`tracker-report.py`) en die file meenemen in dezelfde commit/PR.
