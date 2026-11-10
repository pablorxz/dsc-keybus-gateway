"""Prepare a separate physical HTTPS build using the existing local identity.

This command only renders local files. It does not contact or update a device.
The prior HTTPS build and configuration remain available for recovery.
"""
import argparse
import datetime as dt
import ipaddress
from pathlib import Path
import re
import shutil

from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
from zones_config import render

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / '.local/physical-test'


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Expected firmware template fragment is missing or duplicated.')
    return text.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True, help='Explicit address in the existing certificate')
    parser.add_argument('--suffix', default='next', help='Short local build suffix; existing builds are preserved')
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', args.suffix):
        raise ValueError('Build suffix must contain lowercase letters, numbers and hyphens.')
    address = ipaddress.ip_address(args.address)
    previous = LOCAL / 'https'
    target = LOCAL / ('https-' + args.suffix)
    old_server = previous / 'components/web_server_idf'
    ca = x509.load_pem_x509_certificate((previous/'ca.pem').read_bytes())
    header = (old_server/'bench_tls_credentials.h').read_text(encoding='ascii')
    cert_bytes = re.search(r'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----', header, re.S)
    key_bytes = re.search(r'-----BEGIN PRIVATE KEY-----.*?-----END PRIVATE KEY-----', header, re.S)
    if not cert_bytes or not key_bytes:
        raise ValueError('Existing HTTPS identity is incomplete.')
    cert = x509.load_pem_x509_certificate(cert_bytes.group().encode('ascii'))
    key = serialization.load_pem_private_key(key_bytes.group().encode('ascii'), password=None)
    ca.public_key().verify(cert.signature, cert.tbs_certificate_bytes, ec.ECDSA(cert.signature_hash_algorithm))
    if key.public_key().public_numbers() != cert.public_key().public_numbers():
        raise ValueError('HTTPS private key does not match the certificate.')
    if address not in cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value.get_values_for_type(x509.IPAddress):
        raise ValueError('Existing HTTPS certificate does not cover the requested address.')
    now = dt.datetime.now(dt.timezone.utc)
    if not cert.not_valid_before_utc <= now < cert.not_valid_after_utc or not ca.not_valid_before_utc <= now < ca.not_valid_after_utc:
        raise ValueError('Existing HTTPS certificate or CA is not currently valid.')
    (target/'components').mkdir(parents=True, exist_ok=True)
    shutil.copy2(previous/'ca.pem', target/'ca.pem')
    shutil.copytree(old_server, target/'components/web_server_idf', dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__'))
    web = target/'components/dsc_web'
    shutil.copytree(ROOT/'components/dsc_web', web, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__'))
    cpp = web/'dsc_web.cpp'
    cpp.write_text(replace_once(cpp.read_text(encoding='utf-8'),
                   'std::string("http://")+*host', 'std::string("https://")+*host'), encoding='utf-8')
    config = render((ROOT/'firmware/physical-example.yaml').read_text(encoding='utf-8'), LOCAL/'zones.yaml')
    config = config.replace('@@ROOT@@', ROOT.as_posix())
    config = replace_once(config, '/build/esphome-physical-full"', '/build/esphome-physical-'+args.suffix+'"')
    config = replace_once(config, 'external_components:\n',
                          'external_components:\n  - source:\n      type: local\n      path: "'+(target/'components').as_posix()+'"\n    components: [dsc_web, web_server_idf]\n')
    config = replace_once(config, 'components: [dsc_alarm_panel, dsc_web]', 'components: [dsc_alarm_panel]')
    config = replace_once(config, 'dsc_web:\n  panel_id: dsc_panel', 'dsc_web:\n  port: 443\n  panel_id: dsc_panel')
    (LOCAL/('physical-https-'+args.suffix+'.yaml')).write_text(config, encoding='utf-8')
    print('Prepared a separate HTTPS build with the existing certificate and local zone labels.')


if __name__ == '__main__':
    main()
