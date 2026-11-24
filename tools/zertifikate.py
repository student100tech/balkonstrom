#!/usr/bin/env python3
"""Erzeugt firmware/pvmonitor/zertifikate.h: die Wurzelzertifikate, mit denen
der Monitor GitHub prüft, bevor er eine neue Firmware lädt.

api.github.com und github.com hängen an Sectigo/USERTrust, die Dateien selbst
kommen nach einer Umleitung von *.githubusercontent.com (DigiCert). Zur
Sicherheit sind die Nachfolger-Wurzeln beider Anbieter und Let's Encrypt
dabei - wechselt GitHub den Anbieter, soll das Update nicht still ausfallen.

Aufruf: python3 tools/zertifikate.py [Ordner mit .pem, Standard /etc/ssl/certs]
"""
import sys, pathlib
ORDNER = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '/etc/ssl/certs')
NAMEN = ['USERTrust_ECC_Certification_Authority', 'USERTrust_RSA_Certification_Authority',
         'Sectigo_Public_Server_Authentication_Root_E46', 'Sectigo_Public_Server_Authentication_Root_R46',
         'DigiCert_Global_Root_G2', 'DigiCert_Global_Root_G3', 'ISRG_Root_X1', 'ISRG_Root_X2']
teile = []
for n in NAMEN:
    pem = (ORDNER / f'{n}.pem').read_text().strip()
    assert pem.startswith('-----BEGIN CERTIFICATE-----'), n
    teile.append(f'// {n}\n' + '\n'.join(f'"{z}\\n"' for z in pem.splitlines()))
ziel = pathlib.Path(__file__).resolve().parent.parent / 'firmware' / 'pvmonitor' / 'zertifikate.h'
ziel.write_text('// Erzeugt von tools/zertifikate.py - nicht von Hand ändern.\n#pragma once\n\n'
                'static const char GITHUB_WURZELN[] =\n' + '\n'.join(teile) + ';\n', encoding='utf-8')
print(f'{len(NAMEN)} Wurzeln -> {ziel}')
