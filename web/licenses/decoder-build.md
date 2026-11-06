# Tape Looper Library decoder

This audio-only build uses FFmpeg n5.1.4 and the ffmpeg.wasm worker wrapper. The
archive names and hashes are recorded in sources.json. The Dockerfile pins
Emscripten and lists the enabled demuxers, decoders, filters and PCM output.
The FFmpeg configure report identifies this build as LGPL 2.1 or later.
See the source archives and notices for the component terms.

Prepare a fresh context with Python 3. The helper verifies all three archive
hashes before extraction and refuses to overwrite an existing context.

```text
python3 prepare_context.py /tmp/tape-looper-decoder-context
```

Build the single-threaded browser core with Docker Buildx. Keep the two
generated files together. Compare their hashes against output.sha256.

```sh
cd /tmp/tape-looper-decoder-context
docker buildx build --platform linux/amd64 -f Dockerfile.audio -o ./out .
(cd out && shasum -a 256 -c ../output.sha256)
```

The source archives, pinned inputs and expected output hashes accompany this release.

The Emscripten source archive is supplied for provenance; the build uses the
digest-pinned SDK container. The Dockerfile contains the complete wrapper and
build-script adjustments. No source download from a moving branch occurs.
Docker must have sufficient memory; compilation is bounded to two jobs.

This is an audio-only build, not a reproduction of the old
general-purpose npm core. Matching PCM output was tested for the recorded
audio fixtures. Video processing and arbitrary FFmpeg commands are outside
the Library's supported interface.

## Independent rebuild result

A fresh source compile and relink reproduced both output hashes after explicitly disabling `HAVE_GETRUSAGE`. In these pinned sources that macro is used only by the CLI's benchmark/resource reporting, not audio processing. The browser does not need host process accounting.

The build now runs `verify_config.py` before linking the wrapper. It rejects a tool crash recorded in `config.log` and rejects every other change to the reviewed configuration. It then explicitly normalizes that one unused resource-accounting flag. An earlier Node optimizer SIGSEGV during a configure probe had been mistaken for an unavailable function; this guard prevents accepting such a build silently. This is a build-environment issue, not a device audio fault. Do not force-enable failed feature checks or ignore a configuration mismatch. Inspect the log and rebuild in a stable environment.

The source archives and build scripts accompany the shipped decoder and its notices.
