from pathlib import Path
root=Path(__file__).resolve().parents[1]
cm=(root/'CMakeLists.txt').read_text()
win=(root/'build-windows.ps1').read_text()
term=(root/'scripts/build-termux.sh').read_text()
main=(root/'src/main.cpp').read_text()
checks={
 'version':'APP_VERSION_STRING="5.4-alpha1"' in cm,
 'Windows target':'WAFFLEHOUSE_WINDOWS=1' in cm,
 'Termux target':'WAFFLEHOUSE_TERMUX=1' in cm,
 'Windows builder':'build-windows-msys2.sh' in win,
 'Windows GUI CLI launchers':'wafflehouse-client-gui.cmd' in (root/'scripts/build-windows-msys2.sh').read_text() and 'wafflehouse-client-cli.cmd' in (root/'scripts/build-windows-msys2.sh').read_text(),
 'Termux GUI':'wafflehouse-client-gui' in term,
 'Termux CLI':'wafflehouse-client-cli' in term,
 'shared runtime frontends':'--gui' in main and '--cli' in main,
}
missing=[k for k,v in checks.items() if not v]
if missing: raise SystemExit('FAIL: '+', '.join(missing))
print('WaffleHouse-Client 5.2 platform propagation regression: PASS')
