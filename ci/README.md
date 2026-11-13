# CI definitions

These are the GitHub Actions workflows this project is developed with, kept here
as plain files instead of under `.github/workflows/` because the automation token
used to create this repository is not allowed to add workflow files.

Install them with a token that has the `workflow` scope:

```sh
mkdir -p .github/workflows
cp ci/build.yml.workflow .github/workflows/build.yml
cp ci/tests.yml.workflow .github/workflows/tests.yml
git add .github/workflows && git commit -m "ci: add build and test workflows" && git push
```

What they do:

* **build**: installs `gcc-arm-none-eabi`, runs `make`, `make test`, `make sim`,
  streams three frames from the sender into the simulator, and uploads the
  loader plus the disk images as artefacts.
* **windows-native**: downloads the pinned win32-x64 toolchain (cached between
  runs), then builds the whole OS with `py scripts/build.py all` - the same
  driver `make` uses on Linux, which keeps the Windows path honest.
* **tests**: runs the Python suites only, so pull requests get feedback in a
  couple of seconds without needing a cross compiler.

Both mirror what `make test` does locally; nothing in CI is special.
