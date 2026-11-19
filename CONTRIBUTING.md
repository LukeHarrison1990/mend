# Contributing to Mend

Thanks for helping. Mend is a small project, so every bug report, test result
and pull request makes a real difference.

## Reporting bugs

Open an issue with:

- your operating system and version, and the display scaling you use;
- the host (DAW or editor) and its version, and the plugin format (VST3,
  CLAP, LV2, AU) or the standalone app;
- what you did, what you expected and what happened;
- if it's about the sound, a short clip that shows the problem, if you can
  share one.

## Building and testing

See the *Building* section of the [README](README.md). Before sending a pull
request, run the tests:

```
cmake --build build && ctest --test-dir build --output-on-failure
```

DSP changes should come with a test in `tests/` that checks the behaviour on
a synthetic signal with a known answer, like the existing ones do. "It
sounds better" is a good start, but a number that proves it is what keeps it
better.

## Code

- C++17, no exceptions across the audio thread, no allocation in
  `process()` / `run()`.
- DSP goes in `core/` with no framework dependencies, so the plugins, the
  editor and the tests share it.
- Plugin parameters are only ever appended, never reordered or removed:
  hosts save automation by index.
- Keep the existing formatting (`.clang-format` is in the root).
- New files start with `// SPDX-License-Identifier: MIT` and the copyright
  line used elsewhere.

## Adding a tool

1. Write the DSP class in `core/` with `prepare`, `process` and `latency`.
2. Add a test in `tests/`.
3. Copy a plugin folder in `plugins/` (parameters, DSP wrapper, UI) and add
   it to the `MEND_PLUGINS` list in the top-level `CMakeLists.txt`.
4. Add it to the editor's chain in `app/src/Tools.cpp`.

## Clean-room policy

Mend is an independent, clean-room project. Don't contribute code, assets,
UI artwork, presets or text taken from proprietary products, or anything
made by studying their binaries. Don't name other audio repair products in
the code, UI or docs. Naming what Mend works with is fine: a host it has
been tested in, or the microphone a Voice profile is designed for (those
names stay trademarks of their owners). Published papers, textbooks and
openly licensed code are fine, with credit.

## Licence

By contributing you agree that your work is released under the project's
[MIT licence](LICENSE).
