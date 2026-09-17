from pathlib import Path
root=Path(__file__).resolve().parents[1]
cm=(root/'CMakeLists.txt').read_text()
cast_h=(root/'src/wafflecast.h').read_text()
cast_c=(root/'src/wafflecast.cpp').read_text()
media_h=(root/'src/mediawindow.h').read_text()
media_c=(root/'src/mediawindow.cpp').read_text()
controller_h=(root/'src/mediacontroller.h').read_text()
controller_c=(root/'src/mediacontroller.cpp').read_text()
main=(root/'src/mainwindow.cpp').read_text()
checks={
 '5.4 alpha identity':'APP_VERSION_STRING="5.4-alpha1"' in cm,
 'WaffleCast compiled':'src/wafflecast.cpp' in cm,
 'tokenized stream endpoint':'/wafflecast/%1/%2' in cast_c,
 'multi-listener TCP server':'QTcpServer' in cast_h and 'm_streamClients' in cast_c,
 'ffmpeg live MP3 encoder':'libmp3lame' in cast_c and '128k' in cast_c,
 'metadata endpoint':'meta.json' in cast_c and 'track_generation' in cast_c,
 'cover endpoint':'cover' in cast_c and 'm_coverArt' in cast_c,
 'compact invite frame':'[[WAFFLECAST1:' in cast_c,
 'native invite slash command':'/wafflecast' in main and '/wcast' in main,
 'incoming invite prompt':'WaffleCast Invite' in main and 'joinWaffleCast' in main,
 'outgoing self-echo suppressed':'sender.compare(self, Qt::CaseInsensitive) == 0' in main,
 'secure DM invite handling':'handleWaffleCastPayload(backend, kind, target, result.plaintext)' in main,
 'secure room invite handling':'wafflePrefix + result.plaintext' in main,
 'Album Art UI':'Album Art' in media_c and 'm_albumArt' in media_h,
 'embedded art extraction':'0:v:0' in media_c and 'image2pipe' in media_c,
 'WaffleCast cover polling':'fetchWaffleCastCover' in media_c and 'cover_generation' in media_c,
 'transient listener API':'playTransient' in controller_h and 'm_transientMode' in controller_h,
 'transient playlist protection':'if (m_transientMode) return;' in controller_c,
 'transient selected-index protection':'playlist-pos\") && data.isDouble() && !m_transientMode' in controller_c,
 'WaffleCast uses transient playback':'playTransient(streamUrl.toString' in media_c,
 '5.3 stop regression retained':'m_preserveLibraryWhileStopped' in controller_c,
 '5.3 pause regression retained':'if (m_media->paused())' in media_c,
}
failed=[name for name,ok in checks.items() if not ok]
for name,ok in checks.items(): print(('PASS' if ok else 'FAIL')+' - '+name)
if failed: raise SystemExit('FAIL: '+', '.join(failed))
print(f'\nPASS - {len(checks)} WaffleHouse-Client 5.4 alpha1 WaffleCast/album-art checks')
