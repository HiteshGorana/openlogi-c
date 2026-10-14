Install or update OpenLogi on macOS 13 or newer:

```sh
curl -fsSL https://raw.githubusercontent.com/HiteshGorana/openlogi-c/main/install.sh | sh
```

The installer selects Apple Silicon or Intel binaries, verifies their SHA-256 checksum,
and installs the unified `openlogi` binary alongside backward-compatibility symlinks
(`openlogid`, `openlogictl`, `openlogi_discover`) in `~/.local/bin`. Rerunning it updates
to the latest release and restarts an active installation. Your TOML configuration is retained.

Grant `~/.local/bin/openlogi` Accessibility and Input Monitoring access in
System Settings → Privacy & Security, then run:

```sh
~/.local/bin/openlogi service enable
~/.local/bin/openlogi setup
~/.local/bin/openlogi status
```

These binaries are signed locally with an ad-hoc signature. They are not notarized
with an Apple Developer ID. macOS may require permissions to be granted again after
an update.
