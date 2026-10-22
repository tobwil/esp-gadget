#!/usr/bin/env python3
"""Zeroconf-Kopplung von cspot robuster machen (wird von vendor_cspot.sh aufgerufen).

1. Der DH-Schlüssel für die Kopplung lässt sich von außen setzen und auslesen. Die Uhr speichert ihn,
   sonst ist er nach jedem Neustart neu – die Spotify-App verschlüsselt aber mit dem öffentlichen
   Schlüssel, den sie sich vorher gemerkt hat, und die Uhr kann die Zugangsdaten nicht lesen.
2. Passt die Prüfsumme nicht oder sind die Daten zu kurz, gibt es eine Ausnahme statt eines Absturzes
   (vorher wurde mit unlesbaren Daten weitergerechnet und über das Pufferende hinaus gelesen).

    python3 tools/patch_cspot_zeroconf.py firmware/lib/cspot
"""
import sys
from pathlib import Path

dst = Path(sys.argv[1])


def patch(rel, old, new):
    p = dst / rel
    s = p.read_text()
    if new in s:
        return  # schon gepatcht
    if old not in s:
        sys.exit(f"{rel}: Stelle nicht gefunden: {old[:60]!r}")
    p.write_text(s.replace(old, new, 1))


patch("include/Crypto.h", "  void dhInit();\n",
      "  void dhInit();\n  void dhInitWith(const std::vector<uint8_t>& priv);  // gespeicherten Schlüssel nutzen\n")

crypto = dst / "src/bell/Crypto.cpp"
s = crypto.read_text()
if "dhInitWith" not in s:
    s += '''
// Gespeicherten privaten DH-Schlüssel übernehmen und den öffentlichen daraus berechnen
void CryptoMbedTLS::dhInitWith(const std::vector<uint8_t>& priv) {
  if (priv.size() != DH_KEY_SIZE) return dhInit();
  privateKey = priv;
  mbedtls_mpi prime, generator, res, privKey;
  mbedtls_mpi_init(&prime);
  mbedtls_mpi_init(&generator);
  mbedtls_mpi_init(&privKey);
  mbedtls_mpi_init(&res);
  mbedtls_mpi_read_binary(&prime, DHPrime, sizeof(DHPrime));
  mbedtls_mpi_read_binary(&generator, DHGenerator, sizeof(DHGenerator));
  mbedtls_mpi_read_binary(&privKey, privateKey.data(), DH_KEY_SIZE);
  mbedtls_mpi_exp_mod(&res, &generator, &privKey, &prime, NULL);
  publicKey = std::vector<uint8_t>(DH_KEY_SIZE);
  mbedtls_mpi_write_binary(&res, publicKey.data(), DH_KEY_SIZE);
  mbedtls_mpi_free(&prime);
  mbedtls_mpi_free(&generator);
  mbedtls_mpi_free(&privKey);
  mbedtls_mpi_free(&res);
}
'''
    crypto.write_text(s)

patch("include/LoginBlob.h", " public:\n",
      " public:\n"
      "  // privater DH-Schlüssel der Zeroconf-Kopplung (zum Speichern bzw. Wiederherstellen)\n"
      "  std::vector<uint8_t> dhKey() { return crypto->privateKey; }\n"
      "  void setDhKey(const std::vector<uint8_t>& k) { crypto->dhInitWith(k); }\n")

patch("src/cspot/LoginBlob.cpp", '#include <stdio.h>           // for sprintf\n',
      '#include <stdio.h>           // for sprintf\n#include <stdexcept>         // for runtime_error\n')
patch("src/cspot/LoginBlob.cpp",
      '''  // 0:16 - iv; 17:-20 - blob; -20:0 - checksum
  auto iv''',
      '''  // 0:16 - iv; 17:-20 - blob; -20:0 - checksum
  if (blob.size() < 16 + 20 + 1) throw std::runtime_error("zeroconf blob too short");
  auto iv''')
patch("src/cspot/LoginBlob.cpp",
      '''    CSPOT_LOG(error, "Mac doesn't match!");
  }''',
      '''    CSPOT_LOG(error, "Mac doesn't match!");
    throw std::runtime_error("zeroconf mac mismatch");
  }''')
patch("src/cspot/LoginBlob.cpp",
      '''  auto l = blobData.size();
''',
      '''  auto l = blobData.size();
  if (l < 32) throw std::runtime_error("zeroconf login data too short");
''')
patch("src/cspot/LoginBlob.cpp",
      '''uint32_t LoginBlob::readBlobInt(const std::vector<uint8_t>& data) {
  auto lo''',
      '''uint32_t LoginBlob::readBlobInt(const std::vector<uint8_t>& data) {
  if (blobSkipPosition + 2 > data.size()) throw std::runtime_error("zeroconf login data truncated");
  auto lo''')
patch("src/cspot/LoginBlob.cpp",
      '''  auto authSize = readBlobInt(loginData);
  this->username = username;''',
      '''  auto authSize = readBlobInt(loginData);
  if (blobSkipPosition + authSize > loginData.size()) throw std::runtime_error("zeroconf auth data truncated");
  this->username = username;''')
print("cspot: Zeroconf-Patch angewendet")
