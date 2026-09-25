# Local Emacs Web native build (ABI 1)

Run from `/home/jay/Code/emacs-web-native`. The executable is
`/home/jay/Code/emacs-web-native/src/emacs`; keep its source/build tree in place.
Do not install it. No old `/tmp` checkout or installed Emacs is required.
The existing exporter in `src/emacs-web-display.inc` is unchanged.

## Reproduce on this host

Prerequisites already present: GCC/libgccjit 13.3, GNU Make 4.3, Autoconf
2.71, GTK3 3.24.41, Cairo 1.18.0, HarfBuzz 8.3.0, tree-sitter 0.20.9,
image/font development libraries, and Xvfb. The commands below do not fetch
packages or change system configuration. Missing prerequisites should stop the
build, not trigger downloads or a silently reduced display feature set.

```sh
cd /home/jay/Code/emacs-web-native
mkdir -p build-artifacts/home build-artifacts/tmp
./autogen.sh autoconf > build-artifacts/autogen.log 2>&1
./configure --prefix="$PWD/build-artifacts/unused-install-prefix" \
  --with-x-toolkit=gtk3 --with-cairo \
  --with-native-compilation --with-tree-sitter \
  --without-gconf --without-gsettings --without-sound --without-xwidgets \
  CC=gcc CFLAGS='-O2 -g' > build-artifacts/configure.log 2>&1
HOME="$PWD/build-artifacts/home" TMPDIR="$PWD/build-artifacts/tmp" \
  make -j8 > build-artifacts/make.log 2>&1
```

Run these commands sequentially; stop on any nonzero exit. `autogen.sh autoconf`
avoids its Git-configuration target. The unused prefix stays inside this tree;
**never run `make install`**. Build logs, scratch HOME, temporary files, and
validation outputs are ignored under `build-artifacts/`. Native Lisp build
outputs also live in the ignored `native-lisp/` tree. An incremental rebuild
uses the same `make` command. If the exporter include is later changed, first
`touch src/xdisp.c` so the include is recompiled.

C code uses **`-O2 -g`**, without `-march=native`, LTO, PGO, or unsafe math flags.
Native Lisp compilation is enabled with normal upstream build behavior, not
full ahead-of-time compilation of every bundled Lisp file; runtime
`native-comp-speed` is 2. No special library-path environment is needed on this
host. The isolated HOME keeps build/validation caches away from user dotfiles.

The former recipe in `/home/jay/Code/emacs-web/native/README.md` disabled
native compilation and tree-sitter. Both are now available from existing host
prerequisites. The historical non-image exclusions above are retained.
The fresh build supports X11/GTK3/Cairo, FreeType, HarfBuzz, M17N_FLT, libotf,
PNG, JPEG, GIF, TIFF, SVG/librsvg, WebP, XPM, LCMS2, and normal dynamic modules.
ImageMagick and Xft are not enabled by this Cairo configuration; no previously
explicitly enabled image/font facility was removed. Tree-sitter grammars are
not installed or downloaded by this build.

## Native UI guard

The source now also implements the frame-local `emacs-web-native-ui-blocked` policy and `emacs-web-native-ui-blocked-p` predicate. The display ABI and exporter are unchanged. Rebuild with the incremental command above and restart the serving process; reloading Elisp is not sufficient. The reproducible guard patch is `../emacs-web/native/emacs-30.2-ui.patch`. It rejects unsupported native popup menus/dialogs, X11 menu-bar activation and GTK file/font choosers before toolkit entry; ordinary confirmation/file prompts stay in Emacs's read loop. Only frames explicitly marked by the browser attachment change behavior. Validation and limits are recorded in `../emacs-web/test/native-ui-results.md`.

## Verify without user configuration

```sh
# Required executable/ABI gate:
test -x src/emacs && src/emacs -Q --batch --eval \
  '(unless (and (fboundp (quote emacs-web-native-display)) (= (emacs-web-native-display (quote version)) 1)) (error "Native ABI unavailable"))'

# Actual native compilation, not just a configure claim:
HOME="$PWD/build-artifacts/home" TMPDIR="$PWD/build-artifacts/tmp" \
  ./src/emacs -Q --batch --eval \
  '(progn (require (quote comp)) (unless (and (native-comp-available-p) (treesit-available-p)) (error "Compiler/tree-sitter unavailable")) (let ((fn (native-compile (quote (lambda (x) (+ x 1)))))) (unless (and (native-comp-function-p fn) (= (funcall fn 41) 42)) (error "Native compilation failed")) (princ (format "PASS: native compiled function returned 42; native-comp-speed=%s; tree-sitter available\n" native-comp-speed))))' \
  > build-artifacts/native-comp-smoke.txt 2>&1

# Fresh Xvfb graphical process; no existing editor/server is contacted:
HOME="$PWD/build-artifacts/home" TMPDIR="$PWD/build-artifacts/tmp" \
  NO_AT_BRIDGE=1 \
  EMACS_WEB_NATIVE_SMOKE_REPORT="$PWD/build-artifacts/gui-smoke.txt" \
  timeout 60s xvfb-run -a --server-args='-screen 0 1280x800x24' \
  ./src/emacs -Q --no-splash -l test/manual/emacs-web-native-smoke.el \
  > build-artifacts/xvfb-final.log 2>&1
cat build-artifacts/gui-smoke.txt
```

The smoke registers the stale-display condition normally defined by the HTML
consumer. It verifies graphical X11/Cairo ABI 1, a completed packet surviving
GC, rejection after buffer mutation, and successful recapture after redisplay.
`NO_AT_BRIDGE=1` avoids host accessibility-bus warnings for this isolated Xvfb
test only; it does not alter the build or normal desktop accessibility.

## Fresh results and limits

All three checks above passed with source HEAD `d0e8a175b49` and the unchanged
exporter SHA256
`f63f3bd4db11df5ee7fb2adbd86fbe149ee864d00a7772c3d1dacf660b0a2aad`.
Graphical smoke captured two windows; all seven listed image types were
available. Exact configuration/features are in `build-artifacts/features.txt`;
host/toolchain and executable hashes are in `build-artifacts/toolchain.txt`.

The compiler reports existing `%X` signedness/possible format-overflow warnings
at `src/emacs-web-display.inc:202`, plus unrelated Org manual anchor warnings.
No warning was suppressed and no exporter code was changed. Initial smoke
attempts timed out because the standalone test omitted the consumer's error
registration; the retained final smoke fixes that harness omission.

This is build/ABI validation, **not a fresh performance comparison or complete
GUI fidelity sign-off**. Historical latency/fidelity measurements remain in
`/home/jay/Code/emacs-web/test/native-display-results.md`; they are not results
for this binary. Fresh paired performance/resource results for this executable
are now in `/home/jay/Code/emacs-web/test/performance-results.md`.
For configured launch/testing, use that repository's `test/configured-emacs.sh`
with an ignored `EMACS_WEB_RUN` directory: it redirects native compilation
output before loading the real early-init/init. The owner-ready headless command
in that repository's README adds `-l native/launch.el`, binds loopback18086, uses
the stable seeded `test/artifacts/interactive/eln-cache/`, and loads the renderer
via extensionless `require` (native loading verified). It has no test timeout;
wait for explicit readiness rather than trusting an old URL file. In Emacs 30.2,
`EMACSNATIVELOADPATH` alone does not prevent writes to the user eln-cache.
