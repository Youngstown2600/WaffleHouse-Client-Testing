from pathlib import Path
root=Path(__file__).resolve().parents[1]
cast_h=(root/'src/wafflecast.h').read_text()
cast_c=(root/'src/wafflecast.cpp').read_text()
media=(root/'src/mediawindow.cpp').read_text()
cm=(root/'CMakeLists.txt').read_text()
checks={
 '5.5 identity':'APP_VERSION_STRING="5.5"' in cm,
 'fixed udp discovery port':'discoveryPort() { return 8172; }' in cast_h,
 'host discovery responder':'WAFFLECAST_DISCOVER/1' in cast_c and 'discoveryReadyRead' in cast_c,
 'well known host probe':'/.well-known/wafflecast' in cast_c and 'WaffleCastDiscovery/1' in cast_c,
 'client scans lan first':'WAFFLECAST_DISCOVER/1' in media and 'QHostAddress::Broadcast' in media,
 'custom audio port returned':'stream_url' in cast_c and 'listeningPort()' in cast_c,
 'bare host accepted':'Enter a WaffleCast host' in media and '/.well-known/wafflecast' in media,
 'full token url still supported':'normalizeListenUrl(supplied)' in media,
 'aim invite receiver preserved':'WaffleCast Invite' in (root/'src/mainwindow.cpp').read_text(),
}
failed=[name for name,ok in checks.items() if not ok]
for name,ok in checks.items(): print(('PASS' if ok else 'FAIL'), '-', name)
if failed: raise SystemExit('failed: '+', '.join(failed))
print(f'\nPASS - {len(checks)} WaffleCast LAN/base-URL discovery checks')
