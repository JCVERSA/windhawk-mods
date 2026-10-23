# Contributing

## Mods

Creating, submitting and updating mods is covered in [README.md](README.md)
and the wiki page
[Creating a New Mod](https://github.com/ramensoftware/windhawk/wiki/creating-a-new-mod).
A mod pull request must consist of a single file, `mods/<mod-id>.wh.cpp`.

Mod pull requests are checked automatically:

* `PR mod validation` (`.github/workflows/pr_validation.yml`) — metadata,
  README, settings, symbol hooks and callback signatures.
* `Mod compatibility check` — compiles the mod against several Windhawk
  versions and architectures.
* The label/comment flow in `.github/pr_flow.cjs` — comment `/ai-review`,
  then `/ready-for-reviewer`.

## Repository infrastructure

The tooling in this repository (everything outside `mods/`) generates the
public mod catalog and validates incoming mods:

| File | Purpose |
|---|---|
| `deploy.ts`, `modSourceUtils.ts` | Generates the catalog deployed to the `pages` branch |
| `.github/pr_validation.py` | Validates a mod file in a pull request |
| `.github/preprocessor.py`, `.github/extract_mod_symbols.py` | Extracts hooked symbols from mod sources |
| `.github/pr_flow.cjs` | Label state machine and `/ai-review`, `/ready-for-reviewer` commands |
| `scripts/compile_mod.py` | Compiles a mod with a local Windhawk installation |
| `scripts/archive_mod_images.py` | Archives README images at deploy time |

### Local setup

Requirements (pinned for CI in the workflow files and `.python-version`):

* Python 3.12 (3.11 also works for the validation tooling)
* Node.js 24

```sh
python -m pip install -r .github/requirements.txt
python -m pip install -r .github/requirements-validation.txt
npm ci
```

### Checks

Run these before opening an infrastructure pull request — they are the
same checks as the `Infrastructure validation` workflow:

```sh
python -m unittest discover -s tests -v   # Python tooling tests
npm run typecheck                         # tsc --noEmit (strict)
npm run test:pr-flow                      # PR flow bot tests (node --test)
```

### Running pieces manually

* Validate a single mod file (some checks fetch live data, so results can
  differ from CI):

  ```sh
  python .github/pr_validation.py mods/<mod-id>.wh.cpp <your-github-login>
  ```

* `deploy.ts` is designed to run in the deploy workflow on Windows. It
  requires a full (non-shallow) git checkout and the environment variables
  `GITHUB_TOKEN`, `WINDHAWK_PATH` (a Windhawk installation) and
  `WINDHAWK_MODS_LAST_DEPLOY_PATH` (a checkout of the `pages` branch), and
  it writes generated files into the working tree.

* `scripts/compile_mod.py` also requires a local Windhawk installation;
  see `--help`.
