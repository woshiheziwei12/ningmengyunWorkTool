# MDB Tools native Jet3 reader

This is the same MDB Tools 1.0.1 x86 SDK used by the local Lemon import client.
The SHA-256 of `lib/mdbjet3.lib` is `41b2a10965ed2efbb8af1469de3643ddcbfaa987f79348139fc1216d8b406a9a`.
See `prebuilt-manifest.json` and `COPYING.LIB` for provenance and LGPL-2.0-or-later terms.

Only the native libmdb reader is linked, not the GPL command-line tools. The port
supports UTF-8 Windows paths, source-codepage Jet3 text, explicit read-error
reporting and NULL flags. Keep its four public headers and library together;
upstream headers alone do not match the extended ABI.

The prebuilt library uses the Release dynamic CRT. It is therefore linked only
into the separate Release `LemonJetRecovery.exe`, including when the GUI is built
in Debug. The GUI never loads this static library into its Debug CRT process.
Both configurations build the helper from `Tools/JetRecovery`; there are no
runtime dependencies on the import-client checkout or diagnostic directories.

The manifest names the upstream source archive. The Windows port's patch/build
workspace was not present on this machine; this integration preserves the exact
existing SDK instead of substituting a differently built or incompatible reader.
