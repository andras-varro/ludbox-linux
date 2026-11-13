#!/usr/bin/env bash
# tests/arca_backup_test.sh - host regression for device-files/arca-backup, the selectable backup engine.
#
# Runs the real script under dash (the nearest thing here to BusyBox ash) against a fake root
# (ARCA_ROOT) in a temp dir. Needs GNU tar and gzip; no device, no root. Run it in WSL.
#
# ARCA_BACKUP=<path> points the test at another engine; a missing one must FAIL every check, which is
# how the test was first seen to fail. Cannot see: BusyBox tar's own output format (the link check
# reads `tar tv`, whose name column BusyBox shares with GNU by measurement of the format only).

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="${ARCA_BACKUP:-$HERE/device-files/arca-backup}"
PASS=0
FAIL=0
ok() { PASS=$((PASS + 1)); echo "  ok   $1"; }
bad() { FAIL=$((FAIL + 1)); echo "  FAIL $1"; }
check() { # check <label> <expected> <actual>
  if [ "$2" = "$3" ]; then ok "$1"; else bad "$1"; printf '       want: %s\n       got:  %s\n' "$2" "$3"; fi
}

suite() {
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
R="$W/root"
mkdir -p "$R/tmp" mkdir -p "$R/opt/arca/backup.d" "$R/etc" "$R/home/root/data/ssh" "$R/home/root/.ssh" "$R/opt/games" "$W/tmp"
item() { printf 'name=%s\npaths=%s\nunit=%s\nsecret=%s\nreboot=%s\n' "$2" "$3" "$4" "$5" "$6" > "$R/opt/arca/backup.d/$1.item"; }
item scores "High scores" "home/root/data/*.hig" no no no
item hostname "Host name" "etc/hostname etc/hosts" yes no yes
item ssh "SSH keys" "home/root/.ssh home/root/data/ssh" yes yes yes
item pics "Pictures" "home/root/data/pictures" no no no
printf 'default=no\n' >> "$R/opt/arca/backup.d/pics.item"
item docs "Docs" "home/root/data/docs" no no no
printf 'default=yes\n' >> "$R/opt/arca/backup.d/docs.item"
echo myhost > "$R/etc/hostname"
printf 'VERSION_ID=1.2\n' > "$R/etc/os-release"
echo hosts > "$R/etc/hosts"
echo 100 > "$R/home/root/data/snake.hig"
echo 200 > "$R/home/root/data/tetris.hig"
echo nested > "$R/home/root/data/ssh/key"
echo auth > "$R/home/root/.ssh/authorized_keys"
echo unrelated > "$R/etc/unrelated"
mkdir -p "$R/home/root/data/pictures"; echo jpg > "$R/home/root/data/pictures/a.jpg"

run() { PATH="${SHIM:+$SHIM:}$PATH" ARCA_ROOT="$R" TMPDIR="$W/tmp" dash "$ENGINE" "$@" 2>&1; }

echo "== items"
check "items sorted, flags and names" "item docs unit=no secret=no reboot=no Docs
item hostname unit=yes secret=no reboot=yes Host name
item pics unit=no secret=no reboot=no default=no Pictures
item scores unit=no secret=no reboot=no High scores
item ssh unit=yes secret=yes reboot=yes SSH keys" "$(run items)"
item noted "Noted games" "home/root/data/games" no no no
printf 'note=Only games in this folder are backed up.\n' >> "$R/opt/arca/backup.d/noted.item"
check "note= prints a note line right after that item's line" "item noted unit=no secret=no reboot=no Noted games
note noted Only games in this folder are backed up." "$(run items | grep -A1 '^item noted')"
check "an item without note= gets no note line" "1" "$(run items | grep -c '^note ')"
rm -f "$R/opt/arca/backup.d/noted.item"
OUTP=$(run export "$W/stick/ludbox-backup" pics)
ARCP=${OUTP#exported }
check "default=no item exports like any other" "home/root/data/pictures/a.jpg " "$(tar -tzf "$ARCP" 2> /dev/null | grep jpg | tr '\n' ' ')"
check "contents keeps default=no on the item line" "item pics unit=no secret=no reboot=no default=no Pictures" "$(run contents "$ARCP" | grep '^item')"
echo "== overlapping items"
mkdir -p "$R/home/root/data/roms/nes"; echo rom > "$R/home/root/data/roms/nes/g.nes"; echo opt > "$R/home/root/data/roms/nes/g.nes.opt"
item roms "Games" "home/root/data/roms" no no no
item romopts "Game options" "home/root/data/roms/*/*.opt" no no no
OUTO=$(run export "$W/stick/ludbox-backup" roms romopts)
ARCO=${OUTO#exported }
check "a path in two selected items is archived once" "1" "$(tar -tzf "$ARCO" 2> /dev/null | grep -c 'g\.nes\.opt$')"
rm -f "$R/home/root/data/roms/nes/g.nes" "$R/home/root/data/roms/nes/g.nes.opt"
check "importing both overlapping items restores the files" "imported roms
imported romopts
done reboot=no" "$(run import "$ARCO" roms romopts)"
check "overlap import: rom and option both back" "rom opt" "$(cat "$R/home/root/data/roms/nes/g.nes") $(cat "$R/home/root/data/roms/nes/g.nes.opt")"
rm -rf "$R/home/root/data/roms"
echo "== export and contents"
SNAP=$(cd "$R" && find . | sort)
OUT=$(run export "$W/stick/ludbox-backup" scores hostname)
case "$OUT" in "exported $W/stick/ludbox-backup/ludbox-backup-myhost-"*.tar.gz) ok "export prints the path" ;; *) bad "export prints the path: $OUT" ;; esac
ARC=${OUT#exported }
members=$(tar -tzf "$ARC" 2> /dev/null | sort | tr '\n' ' ')
check "archive members" "etc/hostname etc/hosts home/root/data/snake.hig home/root/data/tetris.hig tmp/ludbox-backup.info " "$members"
check "export leaves the fake root unchanged (no stray file anywhere)" "$SNAP" "$(cd "$R" && find . | sort)"
check "export of an unknown id is refused" "error unknown item nope" "$(run export "$W/x" nope)"
D=$(tar -xzOf "$ARC" tmp/ludbox-backup.info | sed -n 's/^date=//p')
check "contents" "info host=myhost version=1.2 date=$D
item scores unit=no secret=no reboot=no High scores
item hostname unit=yes secret=no reboot=yes Host name" "$(run contents "$ARC")"
check "archives lists it" "archive $(basename "$ARC")" "$(run archives "$W/stick/ludbox-backup")"
check "archives of a missing dir is empty, exit 0" "" "$(run archives "$W/none"; echo -n)"

echo "== archive open reads only the front"
check "export writes the info member first" "tmp/ludbox-backup.info" "$(tar -tzf "$ARC" 2> /dev/null | head -n 1)"
# A new archive with a big incompressible tail, cut to 60%: gzip fails at the cut, so only an open that
# stops after the front member can still list it. The pre-change engine puts info last and loses it.
item bulk "Bulk" "home/root/data/bulk" no no no
mkdir -p "$R/home/root/data/bulk"
head -c 3000000 /dev/urandom > "$R/home/root/data/bulk/blob"
OUTT=$(run export "$W/stickcut/ludbox-backup" bulk)
ARCT=${OUTT#exported }
SZ=$(wc -c < "$ARCT")
head -c $((SZ * 6 / 10)) "$ARCT" > "$W/cut.tar.gz"
check "info is still member 1 with a bulk tail" "tmp/ludbox-backup.info" "$(tar -tzf "$ARCT" 2> /dev/null | head -n 1)"
check "contents of a truncated new archive still lists its items" "item bulk unit=no secret=no reboot=no Bulk" "$(run contents "$W/cut.tar.gz" | grep '^item')"
# Old layout: info last (as archives already in the field are). Opens through the full-read fallback.
mkdir -p "$W/oldl/tmp"
printf 'host=oldhost\nversion=0.9\ndate=20200101-0000\nids=scores\n' > "$W/oldl/tmp/ludbox-backup.info"
(cd "$R" && tar -cf "$W/old.tar" home/root/data/snake.hig && cd "$W/oldl" && tar -rf "$W/old.tar" tmp/ludbox-backup.info)
gzip -c "$W/old.tar" > "$W/ludbox-backup-old.tar.gz"
check "old layout has info last" "tmp/ludbox-backup.info" "$(tar -tzf "$W/ludbox-backup-old.tar.gz" | tail -n 1)"
check "an old-layout archive still opens (fallback)" "info host=oldhost version=0.9 date=20200101-0000
item scores unit=no secret=no reboot=no High scores" "$(run contents "$W/ludbox-backup-old.tar.gz")"
rm -f "$R/home/root/data/snake.hig"
check "an old-layout archive still imports" "imported scores
done reboot=no" "$(run import "$W/ludbox-backup-old.tar.gz" scores)"
check "old-layout import restored the file" "100" "$(cat "$R/home/root/data/snake.hig")"
check "a non-archive is still refused" "error not a backup archive $W/cut.tar.gz.no" "$(echo junk > "$W/cut.tar.gz.no"; run contents "$W/cut.tar.gz.no")"
rm -rf "$R/home/root/data/bulk" "$R/opt/arca/backup.d/bulk.item" "$W/stickcut"

echo "== round trip into a different unit"
R_SAVE="$R"
R="$W/root2"
mkdir -p "$R/opt/arca/backup.d" "$R/etc" "$R/home/root/data"
item scores "High scores" "home/root/data/*.hig" no no no
item hostname "Host name" "etc/hostname etc/hosts" yes no yes
item ssh "SSH keys" "home/root/.ssh" yes yes yes
echo other > "$R/etc/hostname"
echo keepme > "$R/etc/unrelated"
echo old > "$R/home/root/data/old.hig"
check "import reports items and reboot" "imported scores
imported hostname
done reboot=yes" "$(run import "$ARC" scores hostname)"
check "hostname restored" "myhost" "$(cat "$R/etc/hostname")"
check "score restored" "100" "$(cat "$R/home/root/data/snake.hig")"
check "unrelated file kept" "keepme" "$(cat "$R/etc/unrelated")"
check "existing score not deleted" "old" "$(cat "$R/home/root/data/old.hig")"
rm -f "$R/home/root/data/snake.hig"
check "scores alone: reboot=no" "imported scores
done reboot=no" "$(run import "$ARC" scores)"
check "scores alone did not write hostname" "myhost" "$(cat "$R/etc/hostname")"
check "id the archive lacks is refused" "error item not in archive ssh" "$(run import "$ARC" ssh)"
check "id the unit does not declare is refused" "error unknown item bogus" "$(run import "$ARC" bogus)"
rm -f "$R/etc/hosts"
# An item declared by the importing unit narrower than the archive: only declared paths are written.
item hostname "Host name" "etc/hostname" yes no yes
run import "$ARC" hostname > /dev/null
check "undeclared member of a selected id is not written" "no" "$([ -e "$R/etc/hosts" ] && echo yes || echo no)"
R="$R_SAVE"

echo "== hostile archives (python tarfile: GNU tar strips / and .. on create)"
H="$W/hostile"
mkdir -p "$H"
cat > "$H/mk.py" <<'PY'
import io, sys, tarfile
out, kind, ids = sys.argv[1:4]
t = tarfile.open(out, "w:gz")
def f(n, d=b"x"):
    i = tarfile.TarInfo(n); i.size = len(d); t.addfile(i, io.BytesIO(d))
f("tmp/ludbox-backup.info", ("host=h\nversion=1\ndate=d\nids=%s\n" % ids).encode())
S = "home/root/data/scummvm-saves/"
if kind == "abs": f("/tmp/arca-backup-abs-probe")
if kind == "dotdot": f("../../arca-backup-dd-probe")
if kind == "inner": f(S + "../../../../tmp/arca-backup-inner-probe")
if kind == "hard":
    i = tarfile.TarInfo(S + "h"); i.type = tarfile.LNKTYPE; i.linkname = "etc/hostname"; t.addfile(i)
if kind == "sym":
    i = tarfile.TarInfo("home/root/.ssh/lnk"); i.type = tarfile.SYMTYPE; i.linkname = "/tmp"; t.addfile(i)
    f("home/root/.ssh/lnk/arca-backup-sym-probe")
if kind == "clean": f(S + "ok.000", b"saved")
t.close()
PY
item saves "Saves" "home/root/data/scummvm-saves" no no no
item ssh "SSH keys" "home/root/.ssh" yes yes yes
rm -f /tmp/arca-backup-abs-probe /tmp/arca-backup-inner-probe /tmp/arca-backup-sym-probe /tmp/arca-backup-dd-probe
before=$(cd "$R" && find . -type f | sort | xargs md5sum | md5sum)
for k in abs dotdot inner hard sym; do
  ids=saves; [ "$k" = sym ] && ids=ssh
  python3 -I "$H/mk.py" "$H/$k.tar.gz" "$k" "$ids"
  case "$(run import "$H/$k.tar.gz" "$ids" | head -n 1)" in
    "error unsafe member"*) ok "$k: archive refused" ;;
    *) bad "$k: archive refused" ;;
  esac
done
check "refused archives wrote nothing in the root" "$before" "$(cd "$R" && find . -type f | sort | xargs md5sum | md5sum)"
check "no probe file outside the root" "no" "$(find /tmp -maxdepth 1 -name "arca-backup-*-probe" | grep -q . && echo yes || echo no)"
python3 -I "$H/mk.py" "$H/clean.tar.gz" clean saves
check "clean archive without directory entries is accepted" "imported saves
done reboot=no" "$(run import "$H/clean.tar.gz" saves)"
check "its file landed" "saved" "$(cat "$R/home/root/data/scummvm-saves/ok.000" 2> /dev/null)"
check "negative control: a clean archive is accepted" "imported scores
done reboot=no" "$(run import "$ARC" scores)"
echo "== nothing large is staged in TMPDIR (it is a small tmpfs on the unit)"
# tar and gzip wrappers log any file over 512 KB found in TMPDIR before or after each call. The pre-fix
# engine stages out.tar / a.tar there, so it is seen; a streaming engine never is.
mkdir -p "$R/home/root/data/big"
head -c 2097152 /dev/urandom > "$R/home/root/data/big/blob"
item big "Big" "home/root/data/big" no no no
CAP="$W/cap"; CAPLOG="$W/cap.log"; mkdir -p "$CAP"; : > "$CAPLOG"
for t in tar gzip; do
  REAL=$(PATH="${SHIM:+$SHIM:}$PATH" command -v $t)
  # shellcheck disable=SC2016
  printf '#!/bin/sh\nchk() { find "%s" -type f -size +1024 2>/dev/null | head -n 1 >> "%s"; }\nchk\n"%s" "$@"\nrc=$?\nchk\nexit $rc\n' "$W/tmp" "$CAPLOG" "$REAL" > "$CAP/$t"
  chmod +x "$CAP/$t"
done
SHIM_SAVE="$SHIM"; SHIM="$CAP"
OUTB=$(run export "$W/stickbig/ludbox-backup" big)
ARCB=${OUTB#exported }
check "big export succeeds" "exported" "${OUTB%% *}"
check "big export staged nothing large in TMPDIR" "0" "$(grep -c . "$CAPLOG")"
cp "$R/home/root/data/big/blob" "$W/blob.orig"; rm -f "$R/home/root/data/big/blob"
check "big contents lists the item" "item big unit=no secret=no reboot=no Big" "$(run contents "$ARCB" | grep '^item')"
check "big import succeeds" "imported big
done reboot=no" "$(run import "$ARCB" big)"
check "big import restores the file byte for byte" "same" "$(cmp -s "$W/blob.orig" "$R/home/root/data/big/blob" && echo same)"
check "big contents and import staged nothing large in TMPDIR" "0" "$(grep -c . "$CAPLOG")"
check "no partial file left beside the archive" "$(basename "$ARCB")" "$(ls "$W/stickbig/ludbox-backup")"
echo "== a failing stage of the export pipe is an error and leaves no file"
rm -rf "$CAP"; mkdir -p "$CAP"
printf '#!/bin/sh\necho partial\nexit 1\n' > "$CAP/tar"; chmod +x "$CAP/tar"
check "tar failing: error" "error tar failed" "$(run export "$W/fail1/b" big)"
check "tar failing: no archive and no .part" "" "$(ls "$W/fail1/b")"
rm -f "$CAP/tar"
printf '#!/bin/sh\ncat > /dev/null\nexit 1\n' > "$CAP/gzip"; chmod +x "$CAP/gzip"
check "gzip failing: error" "error tar failed" "$(run export "$W/fail2/b" big)"
check "gzip failing: no archive and no .part" "" "$(ls "$W/fail2/b")"
SHIM="$SHIM_SAVE"
echo "== line= item: root's line of etc/shadow"
printf 'name=Root password\nline=etc/shadow:root:\nunit=yes\nsecret=yes\nreboot=no\ndefault=no\n' > "$R/opt/arca/backup.d/rootpw.item"
# shellcheck disable=SC2016
printf 'root:$6$OLD$aaaa:1:0:99999:7:::\ndaemon:*:1:0:99999:7:::\nsvc:$6$X$bbbb:1:0:99999:7:::\n' > "$R/etc/shadow"
chmod 600 "$R/etc/shadow"
check "line item prints with secret=yes default=no" "item rootpw unit=yes secret=yes reboot=no default=no Root password" "$(run items | grep '^item rootpw')"
OUTS=$(run export "$W/stickpw/b" rootpw scores)
ARCS=${OUTS#exported }
check "export carries only the staged root line, not the shadow file" "tmp/ludbox-line-rootpw" "$(tar -tzf "$ARCS" 2> /dev/null | grep -v 'ludbox-backup.info\|\.hig$')"
check "the staged line is root's, one line" "root:\$6\$OLD\$aaaa:1:0:99999:7:::" "$(tar -xzOf "$ARCS" tmp/ludbox-line-rootpw 2> /dev/null)"
check "no staged line left in the root's tmp" "" "$(find "$R/tmp" -name "ludbox-line*")"
# shellcheck disable=SC2016
printf 'root:$6$NEW$zzzz:9:0:99999:7:::\ndaemon:*:5:0:99999:7:::\nother:$6$Y$cccc:2:0:99999:7:::\n' > "$R/etc/shadow"
chmod 640 "$R/etc/shadow"
cp "$R/etc/shadow" "$W/shadow.before"
check "import without the item selected leaves shadow untouched" "same" "$(run import "$ARCS" scores > /dev/null; cmp -s "$W/shadow.before" "$R/etc/shadow" && echo same)"
check "import of rootpw reports done" "imported rootpw
done reboot=no" "$(run import "$ARCS" rootpw)"
check "only root's line changed" "root:\$6\$OLD\$aaaa:1:0:99999:7:::
daemon:*:5:0:99999:7:::
other:\$6\$Y\$cccc:2:0:99999:7:::" "$(cat "$R/etc/shadow")"
check "shadow mode kept" "640" "$(stat -c %a "$R/etc/shadow" 2> /dev/null || stat -f %Lp "$R/etc/shadow")"
check "no temp file left beside shadow" "shadow" "$(find "$R/etc" -name "shadow*" | sed "s,.*/,,")"
printf 'daemon:*:5:0:99999:7:::\n' > "$R/etc/shadow"
run import "$ARCS" rootpw > /dev/null
check "a target without a root line gets it added, the rest kept" "daemon:*:5:0:99999:7:::
root:\$6\$OLD\$aaaa:1:0:99999:7:::" "$(cat "$R/etc/shadow")"
OUTN=$(run export "$W/stickpw/n" scores)
cp "$R/etc/shadow" "$W/shadow.before"
check "an archive without the member: import of rootpw refused (item not in it)" "error item not in archive rootpw" "$(run import "${OUTN#exported }" rootpw)"
rm -f "$R/opt/arca/backup.d/rootpw.item"
rm -rf "$W"
}

for MODE in gnu busybox; do
  echo "######## tar/gzip: $MODE"
  SHIM=""
  if [ "$MODE" = busybox ]; then
    BB="${ARCA_BUSYBOX:-busybox}"
    if ! "$BB" tar --help 2>&1 | grep -q -- "-T"; then echo "skip the busybox pass: $BB tar has no -T (set ARCA_BUSYBOX to a 1.35+ busybox)"; continue; fi
    SHIM="$(mktemp -d)"
    for a in tar gzip; do printf '#!/bin/sh\nexec %s %s "$@"\n' "$BB" "$a" > "$SHIM/$a"; chmod +x "$SHIM/$a"; done
  fi
  suite
  [ -n "$SHIM" ] && rm -rf "$SHIM"
done

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
