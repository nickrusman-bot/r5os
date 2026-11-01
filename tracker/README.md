# R5OS bug tracker

Haiku-tickets blijven op [dev.haiku-os.org](https://dev.haiku-os.org/). Hier houden we bij **wat R5OS daarmee doet**.

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

## GitHub is de lijst

Open Trac-tickets staan als GitHub-issues met label `haiku`. De koppeling zit in `tracker/github.tsv` en in de issue-tekst als `<!-- haiku-ticket:ID -->`. What's new blijft gewone taal. Het issue krijgt de opmerking bij welke build het zit.

```bash
python3 scripts/tracker-import.py ~/Downloads/query.csv
python3 scripts/sync-github-issues.py
python3 scripts/tracker-report.py
```

De sync maakt ontbrekende issues. Een ticket dat op `origin/main` al `fixed` is, wordt gesloten met een comment die de notitie uit `r5-status.tsv` citeert. Een gesloten GitHub-issue gaat niet weer open omdat Trac het nog noemt. Verdwijnt een ticket uit de Trac-snapshot, dan komt er één comment en blijft het hier open. Draai dit opnieuw na een nieuwe CSV.
