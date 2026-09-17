from pathlib import Path
root = Path(__file__).resolve().parents[1]
cast_h = (root/'src/wafflecast.h').read_text()
cast_c = (root/'src/wafflecast.cpp').read_text()
media_h = (root/'src/mediawindow.h').read_text()
media_c = (root/'src/mediawindow.cpp').read_text()
release = (root/'RELEASE-5.5.md').read_text()

checks = {
    'saved broadcast settings button': 'Broadcast Settings' in media_c and 'configureWaffleCastDialog' in media_h,
    'settings persisted': 'Media/WaffleCast/PublicHost' in media_c and 'Media/WaffleCast/ListenPort' in media_c,
    'separate local and wan ports': 'Local listen TCP port:' in media_c and 'Public/WAN TCP port:' in media_c,
    'server supports advertised port': 'quint16 advertisedPort' in cast_h and 'm_advertisedPort' in cast_h,
    'public stream uses advertised port': 'url.setPort(m_advertisedPort ? m_advertisedPort : m_server->serverPort())' in cast_c,
    'discovery advertises local reachability': 'local_port' in cast_c and 'stream_path' in cast_c,
    'lan listener uses udp sender': 'senderHost' in media_c and 'localPort' in media_c and 'NAT reflection/hairpin' in media_c,
    'manual remote probe preserves entered endpoint': 'streamUrl = supplied' in media_c and 'streamUrl.setPath(streamPath)' in media_c,
    'pfSense guidance in UI': 'pfSense WAN TCP 9119' in media_c,
    'invite/public links use public address': 'AIM invites and Copy Public Link' in media_c,
    'release documents internet forwarding': 'Public/WAN' in release and 'port forwarding' in release,
}
failed=[]
for name, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ' - ' + name)
    if not ok: failed.append(name)
if failed:
    raise SystemExit('FAIL: ' + ', '.join(failed))
print(f'\nPASS - {len(checks)} WaffleCast remote/public broadcast checks')
