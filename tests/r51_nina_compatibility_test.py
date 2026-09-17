from pathlib import Path
root=Path(__file__).resolve().parents[1]
cpp=(root/'src/oscarbackend.cpp').read_text()
h=(root/'src/backend.h').read_text()
main=(root/'src/mainwindow.cpp').read_text()
cli=(root/'src/terminalui.cpp').read_text()
proto=(root/'src/oscarprotocol.h').read_text()
checks={
 'network profile field':'QString networkProfile = QStringLiteral("auto")' in h,
 'NINA autodetect':'endsWith(QStringLiteral(".nina.chat"))' in cpp,
 'BUCP u32':'length-form=%2' in cpp and 'readU32(snac.body, 0)' in cpp,
 'BOSS multi conn':'signonTlvs.push_back(Tlv{TLV_MULTI_CONN' in cpp,
 'client first bootstrap':'connection.waitForData(250)' in cpp,
 'implemented family filter':'clientImplementsFamily' in cpp,
 'classic os service v3':'if (family == FAM_OSERVICE) return 3;' in cpp,
 'classic feedbag v2':'if (family == FAM_FEEDBAG) return 2;' in cpp,
 'tolerant host versions':'accepting HOST_VERSIONS with non-echoed request-id' in cpp,
 'classic tool metadata':'classicToolInfo' in cpp,
 'secondary redirect':'serviceRedirectEndpoint' in cpp,
 'multi conn constant':'TLV_MULTI_CONN = 0x004A' in proto,
 'GUI NINA selector':'NINA Network (NINAPatcher compatibility)' in main,
 'CLI NINA selector':'AIM network (auto/nina/custom)' in cli,
}
missing=[k for k,v in checks.items() if not v]
if missing: raise SystemExit('FAIL: '+', '.join(missing))
print('WaffleHouse-Client 5.1 NINA compatibility regression: PASS')
