"""Generate a local experimental physical-panel HTTPS build.

This deliberately patches a private copy of the pinned ESPHome adapter. It is not
a supported production transport. Certificates and keys never leave .local.
"""
import datetime as dt
import ipaddress
import argparse
from pathlib import Path
import shutil

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID
import esphome.components.web_server_idf as adapter
from zones_config import render

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / '.local' / 'physical-test'


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Pinned adapter changed; review the experimental patch before rebuilding.')
    return text.replace(old, new, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--address', required=True, help='Explicit device IP for the test certificate')
    args = parser.parse_args()
    address = ipaddress.ip_address(args.address)
    private = LOCAL / 'https'
    private.mkdir(parents=True, exist_ok=True)
    if (private / 'ca.pem').exists():
        raise RuntimeError('Existing test CA found: preserve it before preparing a new certificate.')
    ca_key = ec.generate_private_key(ec.SECP256R1())
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'DSC HTTPS Test CA')])
    now = dt.datetime.now(dt.timezone.utc)
    ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(ca_key.public_key())
          .serial_number(x509.random_serial_number()).not_valid_before(now-dt.timedelta(days=1))
          .not_valid_after(now+dt.timedelta(days=30))
          .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
          .sign(ca_key, hashes.SHA256()))
    cert = (x509.CertificateBuilder().subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'dsc-physical-full')]))
            .issuer_name(name).public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now-dt.timedelta(days=1)).not_valid_after(now+dt.timedelta(days=14))
            .add_extension(x509.SubjectAlternativeName([x509.IPAddress(address), x509.DNSName('dsc-physical-full.local')]), False)
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
            .sign(ca_key, hashes.SHA256()))
    (private/'ca.pem').write_bytes(ca.public_bytes(serialization.Encoding.PEM))
    cert_pem = cert.public_bytes(serialization.Encoding.PEM).decode('ascii')
    key_pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption()).decode('ascii')
    # The test CA signing key is deliberately not persisted; regenerate for each build.
    override = private / 'components'
    override.mkdir(exist_ok=True)
    server = override / 'web_server_idf'
    web = override / 'dsc_web'
    shutil.copytree(Path(adapter.__file__).parent, server, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__'))
    shutil.copytree(ROOT/'components'/'dsc_web', web, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns('__pycache__'))
    (server/'bench_tls_credentials.h').write_text(
        '#pragma once\nstatic const unsigned char bench_cert[] = R"PEM('+cert_pem+
        ')PEM";\nstatic const unsigned char bench_key[] = R"PEM('+key_pem+')PEM";\n', encoding='ascii')
    init = server/'__init__.py'
    init.write_text(replace_once(init.read_text(encoding='utf-8'),
                    'include_builtin_idf_component("esp_http_server")',
                    'include_builtin_idf_component("esp_http_server")\n'
                    '    include_builtin_idf_component("esp_https_server")\n'
                    '    from esphome.components.esp32 import request_tls, require_mbedtls_tls_server, add_idf_sdkconfig_option\n'
                    '    request_tls()\n    require_mbedtls_tls_server()\n'
                    '    add_idf_sdkconfig_option("CONFIG_MBEDTLS_TLS_CLIENT_ONLY", False)\n'
                    '    add_idf_sdkconfig_option("CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT", True)'), encoding='utf-8')
    cpp = server/'web_server_idf.cpp'
    text = cpp.read_text(encoding='utf-8')
    text = replace_once(text, '#include "web_server_idf.h"',
                        '#include "web_server_idf.h"\n#include <esp_https_server.h>\n#include "bench_tls_credentials.h"')
    text = replace_once(text, 'httpd_stop(this->server_);', 'httpd_ssl_stop(this->server_);')
    text = replace_once(text, 'httpd_config_t config = HTTPD_DEFAULT_CONFIG();\n  config.stack_size = 4512;',
                        'httpd_ssl_config_t ssl = HTTPD_SSL_CONFIG_DEFAULT();\n  httpd_config_t &config = ssl.httpd;\n  config.stack_size = 8192;\n  ssl.servercert = bench_cert;\n  ssl.servercert_len = sizeof(bench_cert);\n  ssl.prvtkey_pem = bench_key;\n  ssl.prvtkey_len = sizeof(bench_key);\n  ssl.port_secure = this->port_;')
    text = replace_once(text, 'httpd_start(&this->server_, &config)', 'httpd_ssl_start(&this->server_, &ssl)')
    cpp.write_text(text, encoding='utf-8')
    web_cpp = web/'dsc_web.cpp'
    web_cpp.write_text(replace_once(web_cpp.read_text(encoding='utf-8'),
                       'std::string("http://")+*host', 'std::string("https://")+*host'), encoding='utf-8')
    config = render((ROOT/'firmware'/'physical-example.yaml').read_text(encoding='utf-8'), LOCAL/'zones.yaml').replace('@@ROOT@@', ROOT.as_posix())
    config = replace_once(config, '/build/esphome-physical-full"', '/build/esphome-physical-https"')
    config = replace_once(config, 'external_components:\n',
                          'external_components:\n  - source:\n      type: local\n      path: "'+override.as_posix()+'"\n    components: [dsc_web, web_server_idf]\n')
    config = replace_once(config, 'components: [dsc_alarm_panel, dsc_web]', 'components: [dsc_alarm_panel]')
    config = replace_once(config, 'dsc_web:\n  panel_id: dsc_panel', 'dsc_web:\n  port: 443\n  panel_id: dsc_panel')
    (LOCAL/'physical-https.yaml').write_text(config, encoding='utf-8')
    print('Prepared private experimental physical-panel HTTPS configuration and test CA.')


if __name__ == '__main__':
    main()
