# Criteri per dichiarare arksh 1.0

Questo documento definisce le condizioni minime che devono essere soddisfatte prima di taggare `v1.0.0`.
Nessun criterio è opzionale; tutti devono essere nello stato `[x]` prima del tag.

---

## 1. Correttezza funzionale

- [ ] Tutti i 333+ test CTest passano su Linux, macOS e Windows senza eccezioni
- [ ] ASan + UBSan build completamente pulita (zero errori, zero leak)
- [ ] Nessuna regressione aperta contro le epoche E1–E15 nel tracker issue
- [ ] Lo script di smoke POSIX (`tests/smoke_posix.sh`) termina con exit 0 in ambiente CI senza TTY

## 2. Compatibilità POSIX / sh

- [ ] `arksh --sh` supera l'intero subset POSIX sh coprente E14 (nessun crash, output corretto)
- [ ] Può essere impostato come shell di login su Linux senza rompere il boot (`/etc/shells` entry + test VM)
- [ ] `ENV` file viene caricato correttamente nelle shell non-interattive `--sh`

## 3. Stabilità interattiva

- [ ] Nessun crash noto nella sessione interattiva su input valido
- [ ] Job control funzionante: `Ctrl-Z`, `fg`, `bg`, `disown` si comportano come bash
- [ ] TTY restaurato correttamente dopo ogni transizione foreground/background
- [ ] Resize terminale (`SIGWINCH`) non rompe il prompt o readline

## 4. Performance

- [ ] Startup wall time ≤ 50 ms in modalità non-interattiva (`arksh_perf_startup_wall_drop` CTest)
- [ ] RSS al startup ≤ 8 MB (misurato con `/usr/bin/time -v` o equivalente)
- [ ] Pipeline con 10 000 elementi completata in ≤ 200 ms su hardware di riferimento (MacBook Air M2 o equivalente)

## 5. Plugin ABI stabile

- [ ] `ARKSH_PLUGIN_ABI_MAJOR` è ≥ 5 e documentato come stabile
- [ ] Un plugin di esempio compilato contro la versione precedente rifiutato dal loader con messaggio chiaro
- [ ] Almeno un plugin di terze parti testato funzionante (anche esempio interno)

## 6. Packaging e distribuzione

- [ ] `brew install nicolorisitano82/arksh/arksh` installa e passa `brew test` su macOS 14+
- [ ] Pacchetto `.deb` installabile su Ubuntu 22.04 LTS e 24.04 LTS
- [ ] Pacchetto `.rpm` installabile su Fedora 40+
- [ ] Manifest winget accettato (o in PR aperta) su `microsoft/winget-pkgs`
- [ ] Man page `arksh(1)` installata e leggibile con `man arksh`

## 7. Documentazione

- [ ] `docs/reference.md` copre tutti i 55 built-in, tutti i 28 stage pipeline, tutte le variabili speciali
- [ ] Guida di installazione testata su Linux, macOS e Windows (seguendo i passi letteralmente)
- [ ] Guida scripting contiene almeno 10 esempi end-to-end funzionanti
- [ ] Guida plugin author permette di scrivere e caricare un plugin hello-world senza guardare il sorgente
- [ ] Sito GitHub Pages pubblicato e navigabile; nessun link rotto (`mkdocs build --strict` pulito)
- [ ] CHANGELOG aggiornato fino al tag 1.0.0

## 8. Sicurezza e robustezza

- [ ] Nessuna vulnerabilità `command injection` nota nell'interprete per input untrusted in `--sh` mode
- [ ] `ulimit` e `umask` rispettati nei subshell
- [ ] `set -e` e `set -u` funzionano correttamente in script non-interattivi
- [ ] Nessun path traversal possibile tramite espansione di variabili nelle operazioni filesystem built-in

## 9. Esperienza d'uso interattivo

- [ ] Completamento tab funzionante per comandi, file e variabili
- [ ] History search (`Ctrl-R`) funzionante
- [ ] Prompt configurabile via `ARKSH_PROMPT` con almeno colori ANSI
- [ ] Integrazione documentata con starship, direnv, fzf, zoxide

## 10. Processo di release

- [ ] Release checklist (`docs/release-checklist.md`) completata integralmente
- [ ] Tutti gli artefatti allegati al GitHub release con checksum pubblicati
- [ ] Tag firmato (`git tag -s`) o annotato
- [ ] Il sito di documentazione aggiornato alla versione 1.0

---

## Stato attuale (pre-1.0, v0.1.0)

| Area | Completato | Note |
|------|-----------|------|
| Correttezza funzionale | ~90% | 333 test passano; smoke POSIX da aggiungere |
| Compatibilità POSIX/sh | ~80% | E14 implementato; test VM non ancora eseguito |
| Stabilità interattiva | ~85% | Job control completo; alcuni edge case TTY aperti |
| Performance | ✓ | Startup ≤ 50 ms verificato da CTest |
| Plugin ABI stabile | ✓ | ABI v5 implementato e testato |
| Packaging | ~70% | Linux+Homebrew+winget skeleton; winget non ancora in winget-pkgs |
| Documentazione | ~80% | Reference e guide complete; sito Pages non ancora pubblicato |
| Sicurezza | ~70% | Nessun audit formale eseguito |
| UX interattiva | ~85% | starship/fzf/zoxide da documentare |
| Processo release | ~60% | Checklist scritta; prima release da eseguire |

Target per v1.0: tutti i criteri `[x]` — stimato dopo E9 completo + audit di sicurezza + test VM.
