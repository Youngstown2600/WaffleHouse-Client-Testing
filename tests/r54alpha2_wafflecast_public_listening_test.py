from pathlib import Path
root=Path(__file__).resolve().parents[1]
cast_h=(root/'src/wafflecast.h').read_text()
cast_c=(root/'src/wafflecast.cpp').read_text()
media_h=(root/'src/mediawindow.h').read_text()
media_c=(root/'src/mediawindow.cpp').read_text()
release=(root/'RELEASE-5.4-alpha2.md').read_text()
checks={
 'manual receiver button':'Connect / Listen' in media_c and 'connectWaffleCastDialog' in media_h,
 'manual URL normalization':'normalizeListenUrl' in cast_h and 'stream.mp3' in cast_c,
 'receiver uses transient playback':'joinWaffleCast(streamUrl' in media_c and 'playTransient' in media_c,
 'public link button':'Copy Public Link' in media_c and 'listenPageUrl' in media_c,
 'browser player endpoint':'<!doctype html>' in cast_c and '<audio controls autoplay src="stream.mp3">' in cast_c,
 'browser live metadata':'fetch(\'meta.json\'' in cast_c and 'cover_generation' in cast_c,
 'direct MP3 endpoint':'Content-Type: audio/mpeg' in cast_c,
 'M3U endpoint':'listen.m3u' in cast_c and '#EXTM3U' in cast_c,
 'PLS endpoint':'listen.pls' in cast_c and '[playlist]' in cast_c,
 'public token remains scoped':'sessionPath(QStringLiteral("listen"))' in cast_c,
 'docs explain non-client listeners':'without WaffleHouse' in release and 'VLC' in release,
}
failed=[]
for name,ok in checks.items():
    print(('PASS' if ok else 'FAIL')+' - '+name)
    if not ok: failed.append(name)
if failed: raise SystemExit('FAIL: '+', '.join(failed))
print(f'\nPASS - {len(checks)} WaffleCast public/manual listening checks')
